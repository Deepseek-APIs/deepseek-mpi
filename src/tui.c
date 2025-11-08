#include "tui.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <ncurses.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "file_loader.h"
#include "string_buffer.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static volatile sig_atomic_t tui_abort_flag = 0;

typedef struct {
  char **lines;
  size_t count;
  size_t capacity;
  size_t prompt_index;
} TuiPromptHistory;

static TuiPromptHistory tui_prompt_history = {0};
static const size_t TUI_HISTORY_MAX_LINES = 1024;
static const int TUI_MIN_INPUT_ROWS = 4; // reserve extra lines for multi-line prompts
static bool tui_log_quiet = false;
static bool tui_history_enabled = false;
static WINDOW *tui_log_window = NULL;

typedef struct {
  WINDOW *outwin;
  WINDOW *inwin;
  int input_start_col;
  bool active;
} ReplUi;

static ReplUi repl_ui = {0};
static const char *REPL_INPUT_PROMPT = "Input (Enter to send, '.' to finish turn): ";

static int tui_prompt_static_rows(void) {
  // Layout rows before the input loop begins.
  return 12;
}

static int tui_prompt_reserved_rows(void) {
  return tui_prompt_static_rows() + TUI_MIN_INPUT_ROWS;
}

static char *tui_history_dup(const char *text, size_t len) {
  if (!text) {
    text = "";
  }
  if (len == (size_t) -1) {
    len = strlen(text);
  }
  char *copy = malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, text, len);
  copy[len] = '\0';
  return copy;
}

static void tui_history_drop_head(TuiPromptHistory *history, size_t lines) {
  if (!history || lines == 0 || history->count == 0) {
    return;
  }
  if (lines >= history->count) {
    for (size_t i = 0; i < history->count; ++i) {
      free(history->lines[i]);
    }
    history->count = 0;
    return;
  }
  for (size_t i = 0; i < lines; ++i) {
    free(history->lines[i]);
  }
  memmove(history->lines, history->lines + lines, (history->count - lines) * sizeof(char *));
  history->count -= lines;
}

static void tui_history_clear(TuiPromptHistory *history) {
  if (!history) {
    return;
  }
  tui_history_drop_head(history, history->count);
  history->prompt_index = 0;
}

static void tui_history_trim_to_limit(TuiPromptHistory *history, size_t limit) {
  if (!history) {
    return;
  }
  if (history->count > limit) {
    size_t extra = history->count - limit;
    tui_history_drop_head(history, extra);
  }
}

static void tui_history_append_line(TuiPromptHistory *history, const char *line, size_t len) {
  if (!history) {
    return;
  }
  if (history->count == history->capacity) {
    size_t new_capacity = history->capacity ? history->capacity * 2 : 64;
    char **lines = realloc(history->lines, new_capacity * sizeof(char *));
    if (!lines) {
      return;
    }
    history->lines = lines;
    history->capacity = new_capacity;
  }
  char *copy = tui_history_dup(line, len);
  if (!copy) {
    return;
  }
  history->lines[history->count++] = copy;
  tui_history_trim_to_limit(history, TUI_HISTORY_MAX_LINES);
}

static void tui_history_append_block(TuiPromptHistory *history, const char *title, const char *text, size_t len) {
  if (!history) {
    return;
  }
  if (!text) {
    text = "";
    len = 0;
  }
  tui_history_append_line(history, title ? title : "", (size_t) -1);
  if (len == (size_t) -1) {
    len = strlen(text);
  }
  const char *cursor = text;
  const char *end = text + len;
  while (cursor < end) {
    const char *newline = memchr(cursor, '\n', (size_t) (end - cursor));
    size_t segment_len = newline ? (size_t) (newline - cursor) : (size_t) (end - cursor);
    while (segment_len > 0 && cursor[segment_len - 1] == '\r') {
      segment_len--;
    }
    tui_history_append_line(history, cursor, segment_len);
    if (!newline) {
      cursor = end;
    } else {
      cursor = newline + 1;
    }
  }
  tui_history_append_line(history, "", 0);
}

static void tui_history_record_prompt(TuiPromptHistory *history, const char *text, size_t len) {
  if (!history || !text || len == 0) {
    return;
  }
  char header[64];
  snprintf(header, sizeof header, "Prompt #%zu:", history->prompt_index + 1);
  tui_history_append_block(history, header, text, len);
  history->prompt_index++;
}

static void tui_history_record_log_entry(LoggerLevel level, int process_rank, const char *timestamp,
                                         const char *message) {
  char prefix[128];
  snprintf(prefix, sizeof prefix, "[%s] %s [rank %d] | ",
           timestamp ? timestamp : "", logger_level_to_string(level), process_rank);
  const char *cursor = message ? message : "";
  bool recorded = false;
  while (1) {
    const char *nl = strchr(cursor, '\n');
    size_t segment_len = nl ? (size_t) (nl - cursor) : strlen(cursor);
    StringBuffer line;
    sb_init(&line);
    sb_append_str(&line, prefix);
    if (segment_len > 0) {
      sb_append(&line, cursor, segment_len);
    }
    tui_history_append_line(&tui_prompt_history,
                            line.data ? line.data : prefix,
                            line.data ? line.length : (size_t) -1);
    sb_clean(&line);
    recorded = true;
    if (!nl) {
      break;
    }
    cursor = nl + 1;
    if (*cursor == '\0') {
      break;
    }
  }
  if (!recorded) {
    tui_history_append_line(&tui_prompt_history, prefix, (size_t) -1);
  }
}

static int tui_history_render(TuiPromptHistory *history, bool enabled, int reserved_rows) {
  if (!enabled || !history || history->count == 0) {
    return 0;
  }
  if (reserved_rows < 0) {
    reserved_rows = 0;
  }
  if (LINES <= reserved_rows + 1) {
    tui_history_clear(history);
    return 0;
  }
  int available = LINES - reserved_rows - 1; // keep separator row
  if (available <= 0) {
    tui_history_clear(history);
    return 0;
  }
  size_t max_lines = (size_t) available;
  if (history->count > max_lines) {
    size_t drop = history->count - max_lines;
    tui_history_drop_head(history, drop);
  }
  int row = 0;
  for (size_t i = 0; i < history->count && row < available; ++i) {
    mvprintw(row++, 2, "%s", history->lines[i]);
  }
  if (row > 0 && row < LINES) {
    row++;
  }
  return row;
}

static void repl_ui_draw_frame(void) {
  int maxy = LINES;
  int maxx = COLS;
  werase(stdscr);
  for (int x = 0; x < maxx; ++x) {
    mvwaddch(stdscr, 0, x, ACS_HLINE);
    mvwaddch(stdscr, maxy - 2, x, ACS_HLINE);
    mvwaddch(stdscr, maxy - 1, x, ACS_HLINE);
  }
  for (int y = 0; y < maxy; ++y) {
    mvwaddch(stdscr, y, 0, ACS_VLINE);
    mvwaddch(stdscr, y, maxx - 1, ACS_VLINE);
  }
  mvwaddch(stdscr, 0, 0, ACS_ULCORNER);
  mvwaddch(stdscr, 0, maxx - 1, ACS_URCORNER);
  mvwaddch(stdscr, maxy - 2, 0, ACS_LLCORNER);
  mvwaddch(stdscr, maxy - 2, maxx - 1, ACS_LRCORNER);
  mvwaddch(stdscr, maxy - 1, 0, ACS_LLCORNER);
  mvwaddch(stdscr, maxy - 1, maxx - 1, ACS_LRCORNER);
  mvwprintw(stdscr, 0, 2, "DeepSeek MPI REPL Output");
  wrefresh(stdscr);
}

static void repl_ui_reset_input(void) {
  if (!repl_ui.active || !repl_ui.inwin) {
    return;
  }
  werase(repl_ui.inwin);
  mvwprintw(repl_ui.inwin, 0, 0, "%s", REPL_INPUT_PROMPT);
  wrefresh(repl_ui.inwin);
}

static void repl_ui_print_line(const char *line) {
  if (!repl_ui.active || !repl_ui.outwin) {
    return;
  }
  if (!line) {
    line = "";
  }
  int y, x;
  getyx(repl_ui.outwin, y, x);
  (void) x;
  wmove(repl_ui.outwin, y, 0);
  wclrtoeol(repl_ui.outwin);
  wmove(repl_ui.outwin, y, 0);
  wprintw(repl_ui.outwin, "%s\n", line);
  wrefresh(repl_ui.outwin);
}

static int repl_ui_create_windows(void) {
  int maxy = LINES;
  int maxx = COLS;
  if (maxy < 4 || maxx < 20) {
    return -1;
  }
  repl_ui.outwin = newwin(maxy - 3, maxx - 2, 1, 1);
  repl_ui.inwin = newwin(1, maxx - 2, maxy - 1, 1);
  if (!repl_ui.outwin || !repl_ui.inwin) {
    if (repl_ui.outwin) {
      delwin(repl_ui.outwin);
      repl_ui.outwin = NULL;
    }
    if (repl_ui.inwin) {
      delwin(repl_ui.inwin);
      repl_ui.inwin = NULL;
    }
    return -1;
  }
  scrollok(repl_ui.outwin, TRUE);
  wmove(repl_ui.outwin, 0, 0);
  repl_ui.input_start_col = (int) strlen(REPL_INPUT_PROMPT);
  tui_log_window = repl_ui.outwin;
  repl_ui_reset_input();
  return 0;
}

static int repl_ui_init(void) {
  if (repl_ui.active) {
    return 0;
  }
  if (!initscr()) {
    return -1;
  }
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  if (has_colors()) {
    start_color();
  }
  repl_ui_draw_frame();
  if (repl_ui_create_windows() != 0) {
    endwin();
    return -1;
  }
  repl_ui.active = true;
  return 0;
}

static void repl_ui_handle_resize(void) {
  if (!repl_ui.active) {
    return;
  }
  endwin();
  refresh();
  repl_ui_draw_frame();
  if (repl_ui.outwin) {
    delwin(repl_ui.outwin);
    repl_ui.outwin = NULL;
  }
  if (repl_ui.inwin) {
    delwin(repl_ui.inwin);
    repl_ui.inwin = NULL;
  }
  if (repl_ui_create_windows() != 0) {
    tui_repl_shutdown();
  }
}

void tui_repl_shutdown(void) {
  if (!repl_ui.active) {
    return;
  }
  if (repl_ui.outwin) {
    delwin(repl_ui.outwin);
    repl_ui.outwin = NULL;
  }
  if (repl_ui.inwin) {
    delwin(repl_ui.inwin);
    repl_ui.inwin = NULL;
  }
  repl_ui.active = false;
  tui_log_window = NULL;
  tui_log_quiet = false;
  endwin();
}

bool tui_repl_attach_logger(Logger *logger) {
  if (!repl_ui.active || !logger) {
    return false;
  }
  tui_log_window = repl_ui.outwin;
  logger_set_sink(logger, tui_logger_sink, NULL);
  logger->mirror_stdout = false;
  return true;
}

void tui_repl_append_assistant(size_t turn, const char *text, size_t len) {
  if (!repl_ui.active) {
    return;
  }
  char header[64];
  snprintf(header, sizeof header, "Assistant #%zu:", turn);
  repl_ui_print_line(header);
  if (!text || len == 0) {
    repl_ui_print_line("(no response)");
  } else {
    const char *cursor = text;
    const char *end = text + len;
    while (cursor < end) {
      const char *nl = memchr(cursor, '\n', (size_t) (end - cursor));
      size_t segment_len = nl ? (size_t) (nl - cursor) : (size_t) (end - cursor);
      char *segment = malloc(segment_len + 1);
      if (!segment) {
        break;
      }
      memcpy(segment, cursor, segment_len);
      segment[segment_len] = '\0';
      repl_ui_print_line(segment_len > 0 ? segment : "");
      free(segment);
      if (!nl) {
        break;
      }
      cursor = nl + 1;
    }
  }
  repl_ui_print_line("");
}
static bool tui_message_contains(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle) {
    return false;
  }
  size_t needle_len = strlen(needle);
  for (const char *p = haystack; *p; ++p) {
    size_t matched = 0;
    while (matched < needle_len && p[matched] &&
           tolower((unsigned char) p[matched]) == tolower((unsigned char) needle[matched])) {
      matched++;
    }
    if (matched == needle_len) {
      return true;
    }
  }
  return false;
}

static bool tui_log_info_allowed(const char *message) {
  if (!message) {
    return false;
  }
  const char *keywords[] = {"response", "assistant", "error", "warning"};
  size_t total = sizeof(keywords) / sizeof(keywords[0]);
  for (size_t i = 0; i < total; ++i) {
    if (tui_message_contains(message, keywords[i])) {
      return true;
    }
  }
  return false;
}

void tui_log_set_quiet(bool quiet) {
  tui_log_quiet = quiet;
}

static void tui_sigint_handler(int signo) {
  (void) signo;
  tui_abort_flag = 1;
}

static void set_error(char **error_out, const char *fmt, ...) {
  if (!error_out) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return;
  }
  size_t size = (size_t) needed + 1;
  char *msg = malloc(size);
  if (!msg) {
    va_end(args);
    return;
  }
  vsnprintf(msg, size, fmt, args);
  va_end(args);
  *error_out = msg;
}

static void append_file_payload(StringBuffer *buffer) {
  if (!buffer || !buffer->data || buffer->length == 0) {
    return;
  }
  if (buffer->data[buffer->length - 1] == '\n') {
    return;
  }
  sb_append_char(buffer, '\n');
}

int tui_capture_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out) {
  if (!config || !output || !output_len) {
    set_error(error_out, "internal: missing argument");
    return -1;
  }

  bool history_enabled = config->repl_mode || config->use_tui_log_view;
  if (!history_enabled) {
    tui_history_clear(&tui_prompt_history);
  }
  tui_history_enabled = history_enabled;

  StringBuffer buffer;
  sb_init(&buffer);

  bool curses_ready = false;
  bool sigint_installed = false;
  struct sigaction old_action;
  char *result = NULL;
  size_t payload_len = 0;
  int rc = -1;

  if (!initscr()) {
    set_error(error_out, "failed to initialize ncurses");
    sb_clean(&buffer);
    return -1;
  }
  curses_ready = true;
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  scrollok(stdscr, TRUE);  // allow manual scroll() calls to push newer input down the screen
  curs_set(1);

  struct sigaction new_action;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = 0;
  new_action.sa_handler = tui_sigint_handler;
  if (sigaction(SIGINT, &new_action, &old_action) == 0) {
    sigint_installed = true;
  }

  int row = 0;
  int reserved_rows = tui_prompt_reserved_rows();
  if (history_enabled) {
    row = tui_history_render(&tui_prompt_history, history_enabled, reserved_rows);
  }
  mvprintw(row++, 2, "DeepSeek MPI Client");
  mvprintw(row++, 2, "Rank 0 interactive mode");
  mvprintw(row++, 2, "Press Ctrl+C to cancel the current line without exiting.");
  row++;
  int preload_prompt_row = row++;
  int preload_input_row = row++;
  int preload_status_row = row++;
  char file_path[PATH_MAX];

  bool preload_complete = false;
  while (!preload_complete) {
    mvprintw(preload_prompt_row, 2, "Enter optional file path to preload (leave empty to skip):");
    move(preload_input_row, 4);
    clrtoeol();
    memset(file_path, 0, sizeof file_path);
    echo();
    getnstr(file_path, (int) sizeof(file_path) - 1);
    noecho();
    if (file_path[0] == '\0') {
      mvprintw(preload_status_row, 2, "No preload file selected.");
      preload_complete = true;
      break;
    }
    if (access(file_path, R_OK) != 0) {
      mvprintw(preload_status_row, 2, "Path '%s' not found (%s). Try again or leave blank to skip.", file_path,
               strerror(errno));
      continue;
    }
    char *file_data = NULL;
    char *err = NULL;
    size_t file_len = 0;
    if (file_loader_read_all(file_path, &file_data, &file_len, &err) != 0) {
      set_error(error_out, "%s", err ? err : "unable to read provided file");
      free(err);
      goto cleanup;
    }
    if (file_data) {
      sb_append(&buffer, file_data, file_len);
      append_file_payload(&buffer);
      free(file_data);
    }
    mvprintw(preload_status_row, 2, "Loaded %s.", file_path);
    preload_complete = true;
  }
  row = preload_status_row + 1;

  row++;
  mvprintw(row++, 2, "Type payload text below. Finish with a single '.' on a line.");
  mvprintw(row++, 2, "Use Backspace to edit. The buffer syncs across MPI ranks after you exit.");
  mvprintw(row++, 2, "Ctrl+C clears the current line; '.' sends the payload to all ranks.");

  int status_row = row++;
  mvprintw(status_row, 2, "Ready.");

  bool collecting = true;
  while (collecting) {
    if (row >= LINES - 2) {
      scroll(stdscr);
      row = LINES - 3;
    }
    move(row, 4);
    clrtoeol();
    char line[2048];
    memset(line, 0, sizeof line);
    echo();
    int rc = getnstr(line, (int) sizeof(line) - 1);
    noecho();
    if (tui_abort_flag) {
      tui_abort_flag = 0;
      mvprintw(status_row, 2, "Ctrl+C detected. Line cleared – continue typing or '.' to finish.");
      clrtoeol();
      continue;
    }
    if (rc == ERR) {
      mvprintw(status_row, 2, "Input error encountered. Try again or press '.' to finish.");
      clrtoeol();
      continue;
    }
    if (strcmp(line, ".") == 0) {
      collecting = false;
      break;
    }
    sb_append_str(&buffer, line);
    sb_append_char(&buffer, '\n');
    row++;
  }

  payload_len = buffer.length;
  result = sb_detach(&buffer);
  if (!result || payload_len == 0) {
    set_error(error_out, "no payload captured");
    free(result);
    result = NULL;
    goto cleanup;
  }

  if (history_enabled) {
    tui_history_record_prompt(&tui_prompt_history, result, payload_len);
  }

  *output = result;
  *output_len = payload_len;
  result = NULL;
  rc = 0;

cleanup:
  sb_clean(&buffer);
  if (curses_ready) {
    endwin();
    curses_ready = false;
  }
  if (sigint_installed) {
    sigaction(SIGINT, &old_action, NULL);
    sigint_installed = false;
  }
  if (rc != 0 && result) {
    free(result);
  }
  return rc;
}

int tui_capture_repl_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out) {
  (void) config;
  if (!output || !output_len) {
    set_error(error_out, "internal: missing argument");
    return -1;
  }
  if (repl_ui_init() != 0) {
    set_error(error_out, "failed to initialize REPL TUI");
    return -1;
  }
  repl_ui_reset_input();

  StringBuffer buffer;
  sb_init(&buffer);
  char line[2048];
  int pos = 0;
  memset(line, 0, sizeof line);

  bool collecting = true;
  while (collecting) {
    int ch = wgetch(repl_ui.inwin);
    if (ch == KEY_RESIZE) {
      repl_ui_handle_resize();
      repl_ui_reset_input();
      pos = 0;
      memset(line, 0, sizeof line);
      continue;
    }
    if (tui_abort_flag) {
      tui_abort_flag = 0;
      repl_ui_reset_input();
      pos = 0;
      memset(line, 0, sizeof line);
      continue;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (pos > 0) {
        pos--;
        line[pos] = '\0';
        mvwdelch(repl_ui.inwin, 0, repl_ui.input_start_col + pos);
        wclrtoeol(repl_ui.inwin);
        wrefresh(repl_ui.inwin);
      }
      continue;
    }
    if (ch == '\n') {
      line[pos] = '\0';
      if (strcmp(line, ".") == 0) {
        collecting = false;
        break;
      }
      sb_append_str(&buffer, line);
      sb_append_char(&buffer, '\n');
      repl_ui_print_line(line);
      pos = 0;
      memset(line, 0, sizeof line);
      repl_ui_reset_input();
      continue;
    }
    if (ch == ERR) {
      continue;
    }
    if (isprint(ch) && pos < (int) sizeof(line) - 1) {
      line[pos++] = (char) ch;
      mvwaddch(repl_ui.inwin, 0, repl_ui.input_start_col + pos - 1, ch);
      wrefresh(repl_ui.inwin);
    }
  }

  size_t payload_len = buffer.length;
  if (payload_len == 0) {
    sb_clean(&buffer);
    set_error(error_out, "no prompt entered");
    return -1;
  }
  char *result = sb_detach(&buffer);
  sb_clean(&buffer);
  if (!result) {
    set_error(error_out, "unable to finalize prompt buffer");
    return -1;
  }
  repl_ui_reset_input();
  *output = result;
  *output_len = payload_len;
  return 0;
}

int tui_log_view_start(void) {
  if (tui_log_window) {
    return 0;
  }
  if (!initscr()) {
    return -1;
  }
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  werase(stdscr);

  int history_rows = tui_history_render(&tui_prompt_history, tui_prompt_history.count > 0, 0);
  int start_row = history_rows;
  if (start_row == 0) {
    start_row = 1;
  }
  if (start_row < LINES) {
    mvprintw(start_row++, 0, "---- MPI Output (Ctrl+C to abort rank 0) ----");
  }
  int height = LINES - start_row;
  if (height <= 0) {
    height = 1;
    start_row = LINES - height;
  }
  tui_log_window = derwin(stdscr, height, COLS, start_row, 0);
  if (!tui_log_window) {
    endwin();
    return -1;
  }
  scrollok(tui_log_window, TRUE);
  wrefresh(stdscr);
  wrefresh(tui_log_window);
  return 0;
}

void tui_log_view_stop(void) {
  if (!tui_log_window) {
    return;
  }
  delwin(tui_log_window);
  tui_log_window = NULL;
  tui_log_quiet = false;
  endwin();
}

void tui_logger_sink(LoggerLevel level, int process_rank, const char *timestamp, const char *message, void *unused) {
  (void) unused;
  if (!tui_log_window) {
    return;
  }
  if (tui_log_quiet && level == LOG_LEVEL_INFO) {
    if (!tui_log_info_allowed(message)) {
      return;
    }
  }
  if (repl_ui.active && tui_log_window == repl_ui.outwin) {
    int row = getcury(tui_log_window);
    if (row <= 0) {
      row = 1;
    }
    wmove(tui_log_window, row, 0);
    wclrtoeol(tui_log_window);
    wmove(tui_log_window, row, 0);
  }
  wattron(tui_log_window, A_NORMAL);
  wprintw(tui_log_window, "[%s] %s [rank %d] | %s\n", timestamp, logger_level_to_string(level), process_rank,
          message ? message : "");
  wrefresh(tui_log_window);
  tui_history_record_log_entry(level, process_rank, timestamp, message);
}
