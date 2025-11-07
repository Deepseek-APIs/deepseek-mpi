#include "tui.h"

#include <limits.h>
#include <ncurses.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "file_loader.h"
#include "string_buffer.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static volatile sig_atomic_t tui_abort_flag = 0;

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

  if (!initscr()) {
    set_error(error_out, "failed to initialize ncurses");
    return -1;
  }
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(1);

  struct sigaction old_action;
  struct sigaction new_action;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = 0;
  new_action.sa_handler = tui_sigint_handler;
  sigaction(SIGINT, &new_action, &old_action);

  int row = 0;
  mvprintw(row++, 2, "DeepSeek MPI Client");
  mvprintw(row++, 2, "Rank 0 interactive mode");
  mvprintw(row++, 2, "Press Ctrl+C to cancel the current line without exiting.");
  row++;
  mvprintw(row++, 2, "Enter optional file path to preload (leave empty to skip):");
  char file_path[PATH_MAX];
  memset(file_path, 0, sizeof file_path);
  echo();
  mvgetnstr(row++, 4, file_path, (int) sizeof(file_path) - 1);
  noecho();

  StringBuffer buffer;
  sb_init(&buffer);

  if (file_path[0] != '\0') {
    char *file_data = NULL;
    char *err = NULL;
    size_t file_len = 0;
    if (file_loader_read_all(file_path, &file_data, &file_len, &err) != 0) {
      endwin();
      set_error(error_out, "%s", err ? err : "unable to read provided file");
      free(err);
      sb_clean(&buffer);
      return -1;
    }
    if (file_data) {
      sb_append(&buffer, file_data, file_len);
      append_file_payload(&buffer);
      free(file_data);
    }
  }

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

  size_t payload_len = buffer.length;
  char *result = sb_detach(&buffer);
  sb_clean(&buffer);
  endwin();
  sigaction(SIGINT, &old_action, NULL);

  if (!result || payload_len == 0) {
    free(result);
    set_error(error_out, "no payload captured");
    return -1;
  }

  *output = result;
  *output_len = payload_len;
  return 0;
}
