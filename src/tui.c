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

#ifndef CTRL
#define CTRL(x) ((x) & 0x1f)
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
static void repl_ui_print_line(const char *line);
static bool repl_ui_handle_prompt_command(const char *line, StringBuffer *buffer, bool *should_exit);

typedef struct {
  WINDOW *outwin;
  WINDOW *file_frame;
  WINDOW *file_win;
  WINDOW *input_frame;
  WINDOW *inwin;
  int input_start_col;
  int file_input_start_col;
  bool active;
  bool focus_on_file;
} ReplUi;

static ReplUi repl_ui = {0};
static const char *REPL_INPUT_PROMPT = "Input (Ctrl+K to send prompt): ";
static const char *REPL_FILE_PROMPT = "Upload file path: ";

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
    mvwaddch(stdscr, maxy - 1, x, ACS_HLINE);
  }
  for (int y = 0; y < maxy; ++y) {
    mvwaddch(stdscr, y, 0, ACS_VLINE);
    mvwaddch(stdscr, y, maxx - 1, ACS_VLINE);
  }
  mvwaddch(stdscr, 0, 0, ACS_ULCORNER);
  mvwaddch(stdscr, 0, maxx - 1, ACS_URCORNER);
  mvwaddch(stdscr, maxy - 1, 0, ACS_LLCORNER);
  mvwaddch(stdscr, maxy - 1, maxx - 1, ACS_LRCORNER);
  mvwprintw(stdscr, 0, 2, "DeepSeek MPI REPL Output");
  wrefresh(stdscr);
}

static void repl_ui_draw_field_frame(WINDOW *frame, const char *title, bool focused) {
  if (!frame) {
    return;
  }
  werase(frame);
  if (focused) {
    wattron(frame, A_BOLD);
  }
  box(frame, 0, 0);
  if (title) {
    mvwprintw(frame, 0, 2, "%s", title);
  }
  if (focused) {
    wattroff(frame, A_BOLD);
  }
  wrefresh(frame);
}

static void repl_ui_update_file_input(const char *text, int cursor_col) {
  if (!repl_ui.file_win) {
    return;
  }
  int len = text ? (int) strlen(text) : 0;
  if (cursor_col < 0 || cursor_col > len) {
    cursor_col = len;
  }
  werase(repl_ui.file_win);
  mvwprintw(repl_ui.file_win, 0, 0, "%s", REPL_FILE_PROMPT);
  if (len > 0) {
    waddnstr(repl_ui.file_win, text, len);
  }
  wclrtoeol(repl_ui.file_win);
  wmove(repl_ui.file_win, 0, repl_ui.file_input_start_col + cursor_col);
  wrefresh(repl_ui.file_win);
}

static void repl_ui_update_prompt_input(const char *text, int cursor_col) {
  if (!repl_ui.inwin) {
    return;
  }
  int len = text ? (int) strlen(text) : 0;
  if (cursor_col < 0 || cursor_col > len) {
    cursor_col = len;
  }
  werase(repl_ui.inwin);
  mvwprintw(repl_ui.inwin, 0, 0, "%s", REPL_INPUT_PROMPT);
  if (len > 0) {
    waddnstr(repl_ui.inwin, text, len);
  }
  wclrtoeol(repl_ui.inwin);
  wmove(repl_ui.inwin, 0, repl_ui.input_start_col + cursor_col);
  wrefresh(repl_ui.inwin);
}

static void repl_ui_set_focus(bool focus_file) {
  repl_ui.focus_on_file = focus_file;
  repl_ui_draw_field_frame(repl_ui.file_frame, " Upload File ", focus_file);
  repl_ui_draw_field_frame(repl_ui.input_frame, " Prompt ", !focus_file);
}

static void repl_ui_print_system_message(const char *text) {
  repl_ui_print_line("System-MPI:");
  if (text && *text) {
    repl_ui_print_line(text);
  }
  repl_ui_print_line("");
}

static void repl_ui_show_help(void) {
  repl_ui_print_line("System-MPI:");
  repl_ui_print_line("DeepSeek MPI chat commands:");
  repl_ui_print_line("  /help                  Show this message");
  repl_ui_print_line("  /quit or /exit         Leave the REPL");
  repl_ui_print_line("  /np <n>                Reminder to restart mpirun with a new -np value");
  repl_ui_print_line("  /clear                 Reset the pending prompt/upload buffer");
  repl_ui_print_line("");
}

static void repl_ui_show_welcome(void) {
  repl_ui_print_system_message(
      "Welcome to the DeepSeek MPI REPL. Type your prompt below; load files above. Use /help for commands.");
  repl_ui_show_help();
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
  if (maxy < 7 || maxx < 20) {
    return -1;
  }
  const int file_frame_height = 3;
  const int prompt_frame_height = 3;
  int out_height = maxy - file_frame_height - prompt_frame_height - 1;
  if (out_height < 2) {
    return -1;
  }
  repl_ui.outwin = newwin(out_height, maxx - 2, 1, 1);
  repl_ui.file_frame = newwin(file_frame_height, maxx - 2, 1 + out_height, 1);
  repl_ui.input_frame = newwin(prompt_frame_height, maxx - 2, 1 + out_height + file_frame_height, 1);
  if (!repl_ui.outwin || !repl_ui.file_frame || !repl_ui.input_frame) {
    if (repl_ui.outwin) {
      delwin(repl_ui.outwin);
      repl_ui.outwin = NULL;
    }
    if (repl_ui.file_frame) {
      delwin(repl_ui.file_frame);
      repl_ui.file_frame = NULL;
    }
    if (repl_ui.input_frame) {
      delwin(repl_ui.input_frame);
      repl_ui.input_frame = NULL;
    }
    return -1;
  }
  repl_ui.file_win = derwin(repl_ui.file_frame, 1, maxx - 4, 1, 1);
  repl_ui.inwin = derwin(repl_ui.input_frame, 1, maxx - 4, 1, 1);
  if (!repl_ui.file_win || !repl_ui.inwin) {
    if (repl_ui.file_win) {
      delwin(repl_ui.file_win);
      repl_ui.file_win = NULL;
    }
    if (repl_ui.inwin) {
      delwin(repl_ui.inwin);
      repl_ui.inwin = NULL;
    }
    delwin(repl_ui.outwin);
    repl_ui.outwin = NULL;
    delwin(repl_ui.file_frame);
    repl_ui.file_frame = NULL;
    delwin(repl_ui.input_frame);
    repl_ui.input_frame = NULL;
    return -1;
  }
  keypad(repl_ui.file_win, TRUE);
  keypad(repl_ui.inwin, TRUE);
  scrollok(repl_ui.outwin, TRUE);
  wmove(repl_ui.outwin, 0, 0);
  repl_ui.input_start_col = (int) strlen(REPL_INPUT_PROMPT);
  repl_ui.file_input_start_col = (int) strlen(REPL_FILE_PROMPT);
  tui_log_window = repl_ui.outwin;
  repl_ui_set_focus(false);
  repl_ui_update_file_input("", 0);
  repl_ui_update_prompt_input("", 0);
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
  nonl();
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
  repl_ui_show_welcome();
  return 0;
}

static void repl_ui_handle_resize(void) {
  if (!repl_ui.active) {
    return;
  }
  endwin();
  refresh();
  nonl();
  repl_ui_draw_frame();
  if (repl_ui.outwin) {
    delwin(repl_ui.outwin);
    repl_ui.outwin = NULL;
  }
  if (repl_ui.file_frame) {
    delwin(repl_ui.file_frame);
    repl_ui.file_frame = NULL;
  }
  if (repl_ui.file_win) {
    delwin(repl_ui.file_win);
    repl_ui.file_win = NULL;
  }
  if (repl_ui.input_frame) {
    delwin(repl_ui.input_frame);
    repl_ui.input_frame = NULL;
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
  if (repl_ui.file_frame) {
    delwin(repl_ui.file_frame);
    repl_ui.file_frame = NULL;
  }
  if (repl_ui.file_win) {
    delwin(repl_ui.file_win);
    repl_ui.file_win = NULL;
  }
  if (repl_ui.input_frame) {
    delwin(repl_ui.input_frame);
    repl_ui.input_frame = NULL;
  }
  if (repl_ui.inwin) {
    delwin(repl_ui.inwin);
    repl_ui.inwin = NULL;
  }
  repl_ui.active = false;
  repl_ui.focus_on_file = false;
  tui_log_window = NULL;
  tui_log_quiet = false;
  nl();
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

  StringBuffer buffer;
  sb_init(&buffer);
  char prompt_line[2048];
  memset(prompt_line, 0, sizeof prompt_line);
  int prompt_pos = 0;
  char file_line[PATH_MAX];
  memset(file_line, 0, sizeof file_line);
  int file_pos = 0;
  repl_ui_update_file_input(file_line, file_pos);
  repl_ui_update_prompt_input(prompt_line, prompt_pos);
  repl_ui_set_focus(false);

  const int CTRL_SEND_KEY = CTRL('K');
  bool collecting = true;
  while (collecting) {
    WINDOW *active = repl_ui.focus_on_file ? repl_ui.file_win : repl_ui.inwin;
    int ch = wgetch(active);
    if (ch == KEY_RESIZE) {
      repl_ui_handle_resize();
      prompt_pos = (int) strlen(prompt_line);
      file_pos = (int) strlen(file_line);
      repl_ui_update_file_input(file_line, file_pos);
      repl_ui_update_prompt_input(prompt_line, prompt_pos);
      repl_ui_set_focus(repl_ui.focus_on_file);
      continue;
    }
    if (tui_abort_flag) {
      tui_abort_flag = 0;
      if (repl_ui.focus_on_file) {
        file_pos = 0;
        file_line[0] = '\0';
        repl_ui_update_file_input(file_line, file_pos);
      } else {
        prompt_pos = 0;
        prompt_line[0] = '\0';
        repl_ui_update_prompt_input(prompt_line, prompt_pos);
      }
      continue;
    }
    if (ch == '\t' || ch == KEY_BTAB) {
      bool next = !repl_ui.focus_on_file;
      repl_ui_set_focus(next);
      repl_ui_update_file_input(file_line, file_pos);
      repl_ui_update_prompt_input(prompt_line, prompt_pos);
      continue;
    }
    if (repl_ui.focus_on_file) {
      if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
        if (file_pos > 0) {
          file_pos--;
          file_line[file_pos] = '\0';
          repl_ui_update_file_input(file_line, file_pos);
        }
        continue;
      }
      if (ch == '\r' || ch == '\n' || ch == KEY_ENTER) {
        file_line[file_pos] = '\0';
        if (file_pos > 0) {
          char *file_data = NULL;
          char *file_err = NULL;
          size_t file_len = 0;
          if (file_loader_read_all(file_line, &file_data, &file_len, &file_err) == 0) {
            if (file_data && file_len > 0) {
              sb_append(&buffer, file_data, file_len);
              if (file_data[file_len - 1] != '\n') {
                sb_append_char(&buffer, '\n');
              }
            }
            char note[256];
            snprintf(note, sizeof note, "[Loaded %s (%zu bytes)]", file_line, file_len);
            repl_ui_print_line(note);
            free(file_data);
          } else {
            repl_ui_print_line(file_err ? file_err : "Unable to read file");
            free(file_err);
          }
        }
        file_pos = 0;
        file_line[0] = '\0';
        repl_ui_update_file_input(file_line, file_pos);
        continue;
      }
      if (ch == ERR) {
        continue;
      }
      if (isprint(ch) && file_pos < (int) sizeof(file_line) - 1) {
        file_line[file_pos++] = (char) ch;
        file_line[file_pos] = '\0';
        repl_ui_update_file_input(file_line, file_pos);
      }
      continue;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      if (prompt_pos > 0) {
        prompt_pos--;
        prompt_line[prompt_pos] = '\0';
        repl_ui_update_prompt_input(prompt_line, prompt_pos);
      }
      continue;
    }
    if (ch == CTRL_SEND_KEY) {
      prompt_line[prompt_pos] = '\0';
      if (prompt_pos > 0) {
        sb_append_str(&buffer, prompt_line);
        sb_append_char(&buffer, '\n');
        repl_ui_print_line(prompt_line);
      }
      prompt_pos = 0;
      prompt_line[0] = '\0';
      repl_ui_update_prompt_input(prompt_line, prompt_pos);
      collecting = false;
      break;
    }
    if (ch == '\r' || ch == '\n' || ch == KEY_ENTER) {
      prompt_line[prompt_pos] = '\0';
      bool exit_requested = false;
      if (prompt_line[0] == '/' &&
          repl_ui_handle_prompt_command(prompt_line, &buffer, &exit_requested)) {
        prompt_pos = 0;
        prompt_line[0] = '\0';
        repl_ui_update_prompt_input(prompt_line, prompt_pos);
        if (exit_requested) {
          collecting = false;
          break;
        }
        continue;
      }
      if (strcmp(prompt_line, ".") == 0) {
        collecting = false;
        break;
      }
      if (prompt_pos > 0) {
        sb_append_str(&buffer, prompt_line);
        sb_append_char(&buffer, '\n');
        repl_ui_print_line(prompt_line);
      }
      prompt_pos = 0;
      prompt_line[0] = '\0';
      repl_ui_update_prompt_input(prompt_line, prompt_pos);
      continue;
    }
    if (ch == ERR) {
      continue;
    }
    if (isprint(ch) && prompt_pos < (int) sizeof(prompt_line) - 1) {
      prompt_line[prompt_pos++] = (char) ch;
      prompt_line[prompt_pos] = '\0';
      repl_ui_update_prompt_input(prompt_line, prompt_pos);
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
  file_line[0] = '\0';
  prompt_line[0] = '\0';
  repl_ui_update_file_input(file_line, 0);
  repl_ui_update_prompt_input(prompt_line, 0);
  repl_ui_set_focus(false);
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
static bool repl_ui_handle_prompt_command(const char *line, StringBuffer *buffer, bool *should_exit) {
  if (!line || line[0] != '/') {
    return false;
  }
  const char *cmd = line + 1;
  while (*cmd == ' ') {
    cmd++;
  }
  if (*cmd == '\0') {
    repl_ui_print_system_message("Enter /help for available commands.");
    return true;
  }
  char keyword[32];
  size_t idx = 0;
  while (cmd[idx] && !isspace((unsigned char) cmd[idx]) && idx < sizeof keyword - 1) {
    keyword[idx] = cmd[idx];
    idx++;
  }
  keyword[idx] = '\0';
  const char *args = cmd + idx;
  while (*args == ' ') {
    args++;
  }
  if (strcasecmp(keyword, "help") == 0) {
    repl_ui_show_help();
    return true;
  }
  if (strcasecmp(keyword, "clear") == 0) {
    if (buffer) {
      sb_reset(buffer);
    }
    repl_ui_print_system_message("Cleared pending prompt buffer.");
    return true;
  }
  if (strcasecmp(keyword, "np") == 0) {
    char note[256];
    const char *np_text = (*args && *args != '\0') ? args : "<n>";
    snprintf(note, sizeof note,
             "This REPL cannot change MPI ranks at runtime. Restart with `mpirun -np %.*s`.", 32, np_text);
    repl_ui_print_system_message(note);
    return true;
  }
  if (strcasecmp(keyword, "quit") == 0 || strcasecmp(keyword, "exit") == 0) {
    if (buffer) {
      sb_reset(buffer);
      sb_append_str(buffer, ":quit");
    }
    if (should_exit) {
      *should_exit = true;
    }
    return true;
  }
  repl_ui_print_system_message("Unknown command. Use /help for the command list.");
  return true;
}
