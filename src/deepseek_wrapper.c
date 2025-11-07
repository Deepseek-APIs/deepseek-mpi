#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <getopt.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "string_buffer.h"

extern char **environ;

#define DEFAULT_BINARY "./src/deepseek_mpi"
#define DEFAULT_RESPONSE_DIR "responses"

typedef struct {
  char role[16];
  char *text;
} Message;

typedef struct {
  Message *items;
  size_t count;
  size_t capacity;
} Conversation;

typedef struct {
  int np;
  char *binary_path;
  char *response_dir;
  size_t chunk_size;
  bool chunk_size_set;
  bool dry_run;
  size_t tasks;
  bool tasks_set;
} WrapperConfig;

typedef enum {
  ATTACH_TEXT,
  ATTACH_BINARY
} AttachmentKind;

static void conversation_free(Conversation *conv) {
  if (!conv) {
    return;
  }
  for (size_t i = 0; i < conv->count; ++i) {
    free(conv->items[i].text);
    conv->items[i].text = NULL;
  }
  free(conv->items);
  conv->items = NULL;
  conv->count = 0;
  conv->capacity = 0;
}

static void conversation_add(Conversation *conv, const char *role, const char *text) {
  if (!conv || !role || !text) {
    return;
  }
  if (conv->count == conv->capacity) {
    size_t next = conv->capacity == 0 ? 8 : conv->capacity * 2;
    Message *tmp = realloc(conv->items, next * sizeof(Message));
    if (!tmp) {
      return;
    }
    conv->items = tmp;
    conv->capacity = next;
  }
  Message *msg = &conv->items[conv->count++];
  snprintf(msg->role, sizeof msg->role, "%s", role);
  msg->text = strdup(text);
  if (!msg->text) {
    msg->text = strdup("<memory error>");
  }
}

static void conversation_clear(Conversation *conv) {
  if (!conv) {
    return;
  }
  conversation_free(conv);
  conv->items = NULL;
  conv->count = 0;
  conv->capacity = 0;
}

static char *lstrip(char *text) {
  while (text && *text && isspace((unsigned char) *text)) {
    ++text;
  }
  return text;
}

static void rstrip(char *text) {
  if (!text) {
    return;
  }
  size_t len = strlen(text);
  while (len > 0 && isspace((unsigned char) text[len - 1])) {
    text[--len] = '\0';
  }
}

static void draw_conversation(WINDOW *outer, WINDOW *inner, const Conversation *conv) {
  if (!outer || !inner || !conv) {
    return;
  }
  werase(outer);
  box(outer, 0, 0);
  werase(inner);
  wmove(inner, 0, 0);
  for (size_t i = 0; i < conv->count; ++i) {
    wattron(inner, A_BOLD);
    wprintw(inner, "%s:\n", conv->items[i].role);
    wattroff(inner, A_BOLD);
    wprintw(inner, "%s\n\n", conv->items[i].text);
  }
  wrefresh(outer);
  wrefresh(inner);
}

static void destroy_windows(WINDOW **conv_outer, WINDOW **conv_inner, WINDOW **output_win, WINDOW **status_win,
                            WINDOW **input_win) {
  if (conv_inner && *conv_inner) {
    delwin(*conv_inner);
    *conv_inner = NULL;
  }
  if (conv_outer && *conv_outer) {
    delwin(*conv_outer);
    *conv_outer = NULL;
  }
  if (output_win && *output_win) {
    delwin(*output_win);
    *output_win = NULL;
  }
  if (status_win && *status_win) {
    delwin(*status_win);
    *status_win = NULL;
  }
  if (input_win && *input_win) {
    delwin(*input_win);
    *input_win = NULL;
  }
}

static void build_windows(WINDOW **conv_outer, WINDOW **conv_inner, WINDOW **output_win, WINDOW **status_win,
                          WINDOW **input_win) {
  destroy_windows(conv_outer, conv_inner, output_win, status_win, input_win);
  int conv_height = LINES - 10;
  if (conv_height < 6) {
    conv_height = 6;
  }
  *conv_outer = newwin(conv_height, COLS, 0, 0);
  *conv_inner = derwin(*conv_outer, conv_height - 2, COLS - 2, 1, 1);
  scrollok(*conv_inner, TRUE);

  int output_height = 4;
  int output_y = conv_height;
  *output_win = newwin(output_height, COLS, output_y, 0);

  int status_y = output_y + output_height;
  *status_win = newwin(3, COLS, status_y, 0);

  int input_y = status_y + 3;
  if (input_y + 3 > LINES) {
    input_y = LINES - 3;
  }
  *input_win = newwin(3, COLS, input_y, 0);
}

static void draw_status(WINDOW *status_win, const char *status) {
  if (!status_win) {
    return;
  }
  werase(status_win);
  box(status_win, 0, 0);
  mvwprintw(status_win, 1, 2, "%s", status ? status : "Ready. Enter text, ENTER to send, :quit to exit.");
  wrefresh(status_win);
}

static void draw_input(WINDOW *input_win, const char *prompt, const char *buffer) {
  if (!input_win) {
    return;
  }
  werase(input_win);
  box(input_win, 0, 0);
  mvwprintw(input_win, 1, 2, "%s %s", prompt, buffer ? buffer : "");
  wmove(input_win, 1, 2 + (int) (prompt ? strlen(prompt) : 0) + (int) (buffer ? strlen(buffer) : 0));
  wrefresh(input_win);
}

static void draw_output(WINDOW *output_win, const StringBuffer *buffer) {
  if (!output_win) {
    return;
  }
  werase(output_win);
  box(output_win, 0, 0);
  mvwprintw(output_win, 0, 2, "Last Output");
  int row = 1;
  int max_rows = getmaxy(output_win) - 1;
  if (buffer && buffer->data) {
    const char *cursor = buffer->data;
    while (*cursor && row < max_rows) {
      const char *nl = strchr(cursor, '\n');
      if (!nl) {
        nl = cursor + strlen(cursor);
      }
      int len = (int) (nl - cursor);
      mvwprintw(output_win, row++, 2, "%.*s", len, cursor);
      if (*nl == '\0') {
        break;
      }
      cursor = nl + 1;
    }
  } else {
    mvwprintw(output_win, row, 2, "(no output)");
  }
  wrefresh(output_win);
}

static const char *guess_attachment_label(const char *path) {
  if (!path) {
    return "attachment";
  }
  const char *dot = strrchr(path, '.');
  if (!dot || *(dot + 1) == '\0') {
    return "attachment";
  }
  const char *ext = dot + 1;
  if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0 ||
      strcasecmp(ext, "gif") == 0 || strcasecmp(ext, "bmp") == 0 || strcasecmp(ext, "webp") == 0) {
    return "image";
  }
  if (strcasecmp(ext, "pdf") == 0 || strcasecmp(ext, "doc") == 0 || strcasecmp(ext, "docx") == 0 ||
      strcasecmp(ext, "ppt") == 0 || strcasecmp(ext, "pptx") == 0) {
    return "document";
  }
  if (strcasecmp(ext, "csv") == 0 || strcasecmp(ext, "xls") == 0 || strcasecmp(ext, "xlsx") == 0 ||
      strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "md") == 0) {
    return "text";
  }
  return "attachment";
}

static AttachmentKind classify_data(const unsigned char *data, size_t len) {
  size_t binary = 0;
  for (size_t i = 0; i < len; ++i) {
    unsigned char ch = data[i];
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      continue;
    }
    if (ch < 0x09 || (ch > 0x0D && ch < 0x20) || ch == 0x7F) {
      binary++;
    }
  }
  if (binary * 5 > len) {
    return ATTACH_BINARY;
  }
  return ATTACH_TEXT;
}

static char *base64_encode(const unsigned char *data, size_t len) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_len = 4 * ((len + 2) / 3);
  char *out = malloc(out_len + 1);
  if (!out) {
    return NULL;
  }
  size_t i = 0, j = 0;
  while (i < len) {
    uint32_t octet_a = i < len ? data[i++] : 0;
    uint32_t octet_b = i < len ? data[i++] : 0;
    uint32_t octet_c = i < len ? data[i++] : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    out[j++] = table[(triple >> 18) & 0x3F];
    out[j++] = table[(triple >> 12) & 0x3F];
    out[j++] = (i > len + 1) ? '=' : table[(triple >> 6) & 0x3F];
    out[j++] = (i > len) ? '=' : table[triple & 0x3F];
  }
  out[out_len] = '\0';
  return out;
}

static int read_file_bytes(const char *path, unsigned char **out, size_t *len, char *errbuf, size_t errlen) {
  if (!path || !out || !len) {
    snprintf(errbuf, errlen, "invalid arguments");
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    snprintf(errbuf, errlen, "unable to open %s: %s", path, strerror(errno));
    return -1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    snprintf(errbuf, errlen, "fseek failed for %s", path);
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if (size < 0) {
    snprintf(errbuf, errlen, "ftell failed for %s", path);
    fclose(fp);
    return -1;
  }
  rewind(fp);
  unsigned char *buffer = malloc((size_t) size + 1);
  if (!buffer) {
    snprintf(errbuf, errlen, "unable to allocate %ld bytes", size);
    fclose(fp);
    return -1;
  }
  size_t read_bytes = fread(buffer, 1, (size_t) size, fp);
  fclose(fp);
  if (read_bytes != (size_t) size) {
    snprintf(errbuf, errlen, "short read for %s", path);
    free(buffer);
    return -1;
  }
  buffer[read_bytes] = '\0';
  *out = buffer;
  *len = read_bytes;
  return 0;
}

static int attach_file_to_conversation(Conversation *conv, const char *path, char *status_line, size_t status_len) {
  unsigned char *bytes = NULL;
  size_t len = 0;
  char errbuf[128];
  if (read_file_bytes(path, &bytes, &len, errbuf, sizeof errbuf) != 0) {
    snprintf(status_line, status_len, "%s", errbuf);
    return -1;
  }
  AttachmentKind kind = classify_data(bytes, len);
  const char *label = guess_attachment_label(path);
  StringBuffer payload;
  sb_init(&payload);
  sb_append_printf(&payload, "Attachment %s (%s, %zu bytes)\n", path, label, len);
  if (kind == ATTACH_TEXT) {
    size_t limit = len > 16384 ? 16384 : len;
    sb_append(&payload, (const char *) bytes, limit);
    if (limit < len) {
      sb_append_str(&payload, "\n... [truncated]\n");
    }
  } else {
    char *encoded = base64_encode(bytes, len);
    if (!encoded) {
      free(bytes);
      sb_clean(&payload);
      snprintf(status_line, status_len, "base64 encode failed");
      return -1;
    }
    sb_append_str(&payload, "Base64:\n");
    sb_append_str(&payload, encoded);
    sb_append_char(&payload, '\n');
    free(encoded);
  }
  conversation_add(conv, "User-Attach", payload.data ? payload.data : "[attachment]");
  sb_clean(&payload);
  free(bytes);
  snprintf(status_line, status_len, "Attached %s", path);
  return 0;
}

static void emit_help(Conversation *conv) {
  conversation_add(conv, "System",
                   "Slash commands:\n"
                   "  /help                  Show this message\n"
                   "  /quit or /exit        Leave the wrapper\n"
                   "  /attach <path>        Attach a document or image (auto text/base64)\n"
                   "  /np <n>               Set MPI ranks for future runs\n"
                   "  /tasks <n>            Request logical task count (auto chunking)\n"
                   "  /chunk <bytes>        Force chunk size\n"
                   "  /dry-run on|off       Toggle dry-run mode\n"
                   "  /clear                Reset the conversation history");
}

static bool handle_command(const char *line, WrapperConfig *cfg, Conversation *conv, char *status_line,
                           size_t status_len, bool *should_quit) {
  if (!line || line[0] != '/') {
    return false;
  }
  char *dup = strdup(line + 1);
  if (!dup) {
    snprintf(status_line, status_len, "out of memory");
    return true;
  }
  char *cmd = lstrip(dup);
  rstrip(cmd);
  char *space = strchr(cmd, ' ');
  char *args = NULL;
  if (space) {
    *space = '\0';
    args = lstrip(space + 1);
  }
  if (strcasecmp(cmd, "help") == 0) {
    emit_help(conv);
    snprintf(status_line, status_len, "Displayed help");
  } else if (strcasecmp(cmd, "quit") == 0 || strcasecmp(cmd, "exit") == 0) {
    *should_quit = true;
    snprintf(status_line, status_len, "Exiting wrapper...");
  } else if (strcasecmp(cmd, "np") == 0) {
    if (!args || !*args) {
      snprintf(status_line, status_len, "Usage: /np <value>");
    } else {
      int value = atoi(args);
      if (value <= 0) {
        snprintf(status_line, status_len, "Invalid np: %s", args);
      } else {
        cfg->np = value;
        snprintf(status_line, status_len, "MPI ranks set to %d", cfg->np);
      }
    }
  } else if (strcasecmp(cmd, "tasks") == 0) {
    if (!args || !*args) {
      snprintf(status_line, status_len, "Usage: /tasks <value>");
    } else {
      unsigned long long value = strtoull(args, NULL, 10);
      if (value == 0) {
        snprintf(status_line, status_len, "Invalid tasks value: %s", args);
      } else {
        cfg->tasks = (size_t) value;
        cfg->tasks_set = true;
        snprintf(status_line, status_len, "Tasks set to %zu", cfg->tasks);
      }
    }
  } else if (strcasecmp(cmd, "chunk") == 0) {
    if (!args || !*args) {
      snprintf(status_line, status_len, "Usage: /chunk <bytes>");
    } else {
      unsigned long long value = strtoull(args, NULL, 10);
      if (value == 0) {
        snprintf(status_line, status_len, "Invalid chunk size: %s", args);
      } else {
        cfg->chunk_size = (size_t) value;
        cfg->chunk_size_set = true;
        snprintf(status_line, status_len, "Chunk size set to %zu bytes", cfg->chunk_size);
      }
    }
  } else if (strcasecmp(cmd, "dry-run") == 0) {
    if (!args || !*args) {
      cfg->dry_run = !cfg->dry_run;
      snprintf(status_line, status_len, "Dry-run toggled %s", cfg->dry_run ? "on" : "off");
    } else if (strcasecmp(args, "on") == 0) {
      cfg->dry_run = true;
      snprintf(status_line, status_len, "Dry-run enabled");
    } else if (strcasecmp(args, "off") == 0) {
      cfg->dry_run = false;
      snprintf(status_line, status_len, "Dry-run disabled");
    } else {
      snprintf(status_line, status_len, "Usage: /dry-run [on|off]");
    }
  } else if (strcasecmp(cmd, "attach") == 0) {
    if (!args || !*args) {
      snprintf(status_line, status_len, "Usage: /attach <path>");
    } else {
      attach_file_to_conversation(conv, args, status_line, status_len);
    }
  } else if (strcasecmp(cmd, "clear") == 0) {
    conversation_clear(conv);
    conversation_add(conv, "System", "Conversation cleared. Start a new session.");
    snprintf(status_line, status_len, "Conversation cleared");
  } else {
    snprintf(status_line, status_len, "Unknown command: /%s", cmd);
  }
  free(dup);
  return true;
}

static int ensure_response_dir(const char *path, char *errbuf, size_t errlen) {
  if (!path || !*path) {
    return 0;
  }
  struct stat st;
  if (stat(path, &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      snprintf(errbuf, errlen, "%s exists but is not a directory", path);
      return -1;
    }
    return 0;
  }
  if (mkdir(path, 0755) != 0) {
    snprintf(errbuf, errlen, "mkdir %s failed: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}

static int write_payload_file(const Conversation *conv, char *template_path) {
  if (!conv || !template_path) {
    return -1;
  }
  int fd = mkstemp(template_path);
  if (fd == -1) {
    return -1;
  }
  FILE *fp = fdopen(fd, "w");
  if (!fp) {
    close(fd);
    unlink(template_path);
    return -1;
  }
  for (size_t i = 0; i < conv->count; ++i) {
    fprintf(fp, "%s: %s\n\n", conv->items[i].role, conv->items[i].text);
  }
  fclose(fp);
  return 0;
}

static int spawn_and_capture(char *const argv[], StringBuffer *output, char *errbuf, size_t errlen) {
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    snprintf(errbuf, errlen, "pipe failed: %s", strerror(errno));
    return -1;
  }
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  posix_spawn_file_actions_addclose(&actions, pipefd[1]);

  pid_t pid;
  int rc = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);
  if (rc != 0) {
    snprintf(errbuf, errlen, "posix_spawnp failed: %s", strerror(rc));
    close(pipefd[0]);
    return -1;
  }

  char buffer[4096];
  ssize_t read_bytes;
  while ((read_bytes = read(pipefd[0], buffer, sizeof buffer)) > 0) {
    sb_append(output, buffer, (size_t) read_bytes);
  }
  close(pipefd[0]);

  int status;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return 0;
  }
  snprintf(errbuf, errlen, "mpirun exited with status %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
  return -1;
}

static int build_command(const WrapperConfig *cfg, const char *payload_path, char **argv, size_t max_args) {
  if (!cfg || !payload_path || !argv) {
    return -1;
  }
  size_t idx = 0;
  if (idx + 1 >= max_args) {
    return -1;
  }
  argv[idx++] = "mpirun";
  argv[idx++] = "-np";
  static char np_buf[16];
  snprintf(np_buf, sizeof np_buf, "%d", cfg->np);
  argv[idx++] = np_buf;
  argv[idx++] = cfg->binary_path;
  argv[idx++] = "--input-file";
  argv[idx++] = (char *) payload_path;
  if (cfg->response_dir) {
    argv[idx++] = "--response-dir";
    argv[idx++] = cfg->response_dir;
  }
  static char chunk_buf[32];
  if (cfg->chunk_size_set) {
    snprintf(chunk_buf, sizeof chunk_buf, "%zu", cfg->chunk_size);
    argv[idx++] = "--chunk-size";
    argv[idx++] = chunk_buf;
  }
  static char tasks_buf[32];
  if (cfg->tasks_set) {
    snprintf(tasks_buf, sizeof tasks_buf, "%zu", cfg->tasks);
    argv[idx++] = "--tasks";
    argv[idx++] = tasks_buf;
  }
  if (cfg->dry_run) {
    argv[idx++] = "--dry-run";
  }
  argv[idx] = NULL;
  return 0;
}

static int run_inference(const WrapperConfig *cfg, Conversation *conv, StringBuffer *last_output, char *status_buf,
                         size_t status_len) {
  if (!cfg || !conv) {
    snprintf(status_buf, status_len, "internal error: missing cfg");
    return -1;
  }
  char payload_path[] = "/tmp/deepseek_payloadXXXXXX";
  if (write_payload_file(conv, payload_path) != 0) {
    snprintf(status_buf, status_len, "unable to create payload file");
    return -1;
  }

  char *argv[32];
  if (build_command(cfg, payload_path, argv, sizeof argv / sizeof(argv[0])) != 0) {
    snprintf(status_buf, status_len, "failed to prepare mpi command");
    unlink(payload_path);
    return -1;
  }

  StringBuffer response;
  sb_init(&response);
  char errbuf[128];
  int rc = spawn_and_capture(argv, &response, errbuf, sizeof errbuf);
  unlink(payload_path);

  if (rc != 0) {
    snprintf(status_buf, status_len, "%s", errbuf);
    sb_clean(&response);
    return -1;
  }
  if (response.length == 0) {
    sb_append_str(&response, "(no output)\n");
  }
  if (last_output) {
    sb_reset(last_output);
    size_t limit = response.length > 8192 ? 8192 : response.length;
    sb_append(last_output, response.data, limit);
    if (limit < response.length) {
      sb_append_str(last_output, "\n... [truncated]\n");
    }
  }
  conversation_add(conv, "DeepSeek", response.data);
  sb_clean(&response);
  snprintf(status_buf, status_len, "DeepSeek run completed.");
  return 0;
}

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [options]\n\n"
          "Options:\n"
          "  --np N                Number of MPI ranks (default 2)\n"
          "  --binary PATH         Path to deepseek_mpi binary (default %s)\n"
          "  --response-dir DIR    Directory for chunk responses (default %s)\n"
          "  --chunk-size BYTES    Override chunk size\n"
          "  --tasks N             Default logical task count (auto chunking)\n"
          "  --dry-run             Pass --dry-run to deepseek_mpi\n"
          "  --help                Show this message\n"
          "Slash commands inside the UI: /help, /attach <file>, /np <n>, /tasks <n>, /chunk <bytes>, /dry-run on|off, /clear, /quit\n",
          prog, DEFAULT_BINARY, DEFAULT_RESPONSE_DIR);
}

static void wrapper_defaults(WrapperConfig *cfg) {
  cfg->np = 2;
  cfg->binary_path = strdup(DEFAULT_BINARY);
  cfg->response_dir = strdup(DEFAULT_RESPONSE_DIR);
  cfg->chunk_size = 2048;
  cfg->chunk_size_set = false;
  cfg->dry_run = false;
  cfg->tasks = 0;
  cfg->tasks_set = false;
}

int main(int argc, char **argv) {
  WrapperConfig cfg;
  wrapper_defaults(&cfg);

  static struct option long_opts[] = {{"np", required_argument, NULL, 'n'},
                                      {"binary", required_argument, NULL, 'b'},
                                      {"response-dir", required_argument, NULL, 'r'},
                                      {"chunk-size", required_argument, NULL, 'c'},
                                      {"tasks", required_argument, NULL, 'w'},
                                      {"dry-run", no_argument, NULL, 'd'},
                                      {"help", no_argument, NULL, 'h'},
                                      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "n:b:r:c:w:dh", long_opts, NULL)) != -1) {
    switch (opt) {
    case 'n':
      cfg.np = atoi(optarg);
      if (cfg.np <= 0) {
        fprintf(stderr, "Invalid np value: %s\n", optarg);
        return EXIT_FAILURE;
      }
      break;
    case 'b':
      free(cfg.binary_path);
      cfg.binary_path = strdup(optarg);
      break;
    case 'r':
      free(cfg.response_dir);
      cfg.response_dir = strdup(optarg);
      break;
    case 'c':
      cfg.chunk_size = (size_t) strtoull(optarg, NULL, 10);
      cfg.chunk_size_set = true;
      break;
    case 'w':
      cfg.tasks = (size_t) strtoull(optarg, NULL, 10);
      cfg.tasks_set = cfg.tasks > 0;
      if (!cfg.tasks_set) {
        fprintf(stderr, "Invalid tasks value: %s\n", optarg);
        return EXIT_FAILURE;
      }
      break;
    case 'd':
      cfg.dry_run = true;
      break;
    case 'h':
      usage(argv[0]);
      return EXIT_SUCCESS;
    default:
      usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  char errbuf[128];
  if (ensure_response_dir(cfg.response_dir, errbuf, sizeof errbuf) != 0) {
    fprintf(stderr, "%s\n", errbuf);
    return EXIT_FAILURE;
  }

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(1);

  WINDOW *conversation_outer = NULL;
  WINDOW *conversation_inner = NULL;
  WINDOW *output_win = NULL;
  WINDOW *status_win = NULL;
  WINDOW *input_win = NULL;
  build_windows(&conversation_outer, &conversation_inner, &output_win, &status_win, &input_win);

  Conversation conv = {0};
  conversation_add(&conv, "System",
                   "Welcome to the DeepSeek wrapper. Type prompts to run inference or use /help for meta-commands.");

  char status_line[256] = "Ready.";
  StringBuffer current_input;
  sb_init(&current_input);
  StringBuffer last_output;
  sb_init(&last_output);

  bool running = true;
  while (running) {
    draw_conversation(conversation_outer, conversation_inner, &conv);
    draw_output(output_win, &last_output);
    draw_status(status_win, status_line);
    draw_input(input_win, "You>", current_input.data ? current_input.data : "");

    int ch = wgetch(input_win);
    if (ch == ERR) {
      continue;
    }
    if (ch == KEY_RESIZE) {
      build_windows(&conversation_outer, &conversation_inner, &output_win, &status_win, &input_win);
      continue;
    }
    if (ch == '\n') {
      const char *line = current_input.data ? current_input.data : "";
      bool should_quit = false;
      if (line[0] == '/') {
        handle_command(line, &cfg, &conv, status_line, sizeof status_line, &should_quit);
        sb_reset(&current_input);
        if (should_quit) {
          running = false;
        }
        continue;
      }
      if (strcmp(line, ":quit") == 0) {
        running = false;
      } else if (strlen(line) == 0) {
        snprintf(status_line, sizeof status_line, "Please enter a prompt or /help.");
      } else {
        conversation_add(&conv, "User", line);
        sb_reset(&current_input);
        snprintf(status_line, sizeof status_line, "Running DeepSeek...");
        draw_status(status_win, status_line);
        wrefresh(status_win);
        run_inference(&cfg, &conv, &last_output, status_line, sizeof status_line);
      }
      sb_reset(&current_input);
      continue;
    }
    if (ch == 27) { // ESC
      running = false;
      continue;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
      if (current_input.length > 0) {
        current_input.data[--current_input.length] = '\0';
      }
      continue;
    }
    if (ch >= 32 && ch < 126) {
      sb_append_char(&current_input, (char) ch);
    }
  }

  endwin();
  destroy_windows(&conversation_outer, &conversation_inner, &output_win, &status_win, &input_win);
  conversation_free(&conv);
  sb_clean(&current_input);
  sb_clean(&last_output);
  free(cfg.binary_path);
  free(cfg.response_dir);
  return EXIT_SUCCESS;
}
