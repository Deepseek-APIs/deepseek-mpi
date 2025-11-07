#include <curses.h>
#include <errno.h>
#include <getopt.h>
#include <spawn.h>
#include <stdbool.h>
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
} WrapperConfig;

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
  if (cfg->dry_run) {
    argv[idx++] = "--dry-run";
  }
  argv[idx] = NULL;
  return 0;
}

static int run_inference(const WrapperConfig *cfg, Conversation *conv, char *status_buf, size_t status_len) {
  if (!cfg || !conv) {
    snprintf(status_buf, status_len, "internal error: missing cfg");
    return -1;
  }
  char payload_path[] = "/tmp/codex_payloadXXXXXX";
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
          "  --dry-run             Pass --dry-run to deepseek_mpi\n"
          "  --help                Show this message\n",
          prog, DEFAULT_BINARY, DEFAULT_RESPONSE_DIR);
}

static void wrapper_defaults(WrapperConfig *cfg) {
  cfg->np = 2;
  cfg->binary_path = strdup(DEFAULT_BINARY);
  cfg->response_dir = strdup(DEFAULT_RESPONSE_DIR);
  cfg->chunk_size = 2048;
  cfg->chunk_size_set = false;
  cfg->dry_run = false;
}

int main(int argc, char **argv) {
  WrapperConfig cfg;
  wrapper_defaults(&cfg);

  static struct option long_opts[] = {{"np", required_argument, NULL, 'n'},
                                      {"binary", required_argument, NULL, 'b'},
                                      {"response-dir", required_argument, NULL, 'r'},
                                      {"chunk-size", required_argument, NULL, 'c'},
                                      {"dry-run", no_argument, NULL, 'd'},
                                      {"help", no_argument, NULL, 'h'},
                                      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "n:b:r:c:dh", long_opts, NULL)) != -1) {
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

  int height = LINES - 6;
  WINDOW *conversation_outer = newwin(height, COLS, 0, 0);
  WINDOW *conversation_inner = derwin(conversation_outer, height - 2, COLS - 2, 1, 1);
  scrollok(conversation_inner, TRUE);

  WINDOW *status_win = newwin(3, COLS, height, 0);
  WINDOW *input_win = newwin(3, COLS, height + 3, 0);

  Conversation conv = {0};
  conversation_add(&conv, "System", "Welcome to the DeepSeek Codex wrapper. Describe your intent and press Enter.");

  char status_line[256] = "Ready.";
  StringBuffer current_input;
  sb_init(&current_input);

  bool running = true;
  while (running) {
    draw_conversation(conversation_outer, conversation_inner, &conv);
    draw_status(status_win, status_line);
    draw_input(input_win, "You>", current_input.data ? current_input.data : "");

    int ch = wgetch(input_win);
    if (ch == ERR) {
      continue;
    }
    if (ch == KEY_RESIZE) {
      height = LINES - 6;
      delwin(conversation_inner);
      delwin(conversation_outer);
      delwin(status_win);
      delwin(input_win);

      conversation_outer = newwin(height, COLS, 0, 0);
      conversation_inner = derwin(conversation_outer, height - 2, COLS - 2, 1, 1);
      scrollok(conversation_inner, TRUE);
      status_win = newwin(3, COLS, height, 0);
      input_win = newwin(3, COLS, height + 3, 0);
      continue;
    }
    if (ch == '\n') {
      const char *line = current_input.data ? current_input.data : "";
      if (strcmp(line, ":quit") == 0) {
        running = false;
      } else if (strlen(line) == 0) {
        snprintf(status_line, sizeof status_line, "Please enter a prompt or :quit.");
      } else {
        conversation_add(&conv, "User", line);
        sb_reset(&current_input);
        snprintf(status_line, sizeof status_line, "Running DeepSeek...");
        draw_status(status_win, status_line);
        wrefresh(status_win);
        run_inference(&cfg, &conv, status_line, sizeof status_line);
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
  conversation_free(&conv);
  sb_clean(&current_input);
  free(cfg.binary_path);
  free(cfg.response_dir);
  return EXIT_SUCCESS;
}
