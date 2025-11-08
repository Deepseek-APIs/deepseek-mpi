#include "readline_prompt.h"

#include "app_config.h"
#include "string_buffer.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>

#include <readline/history.h>
#include <readline/readline.h>

static void rl_set_error(char **error_out, const char *fmt, ...) {
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
  size_t len = (size_t) needed + 1;
  char *msg = malloc(len);
  if (!msg) {
    va_end(args);
    return;
  }
  vsnprintf(msg, len, fmt, args);
  va_end(args);
  *error_out = msg;
}

static void print_readline_banner(void) {
  fprintf(stdout,
          "\nDeepSeek MPI Readline Mode\n"
          "------------------------------------\n"
          "- Type your prompt; finish with a single '.' on its own line.\n"
          "- Use Ctrl+D to abort, or Ctrl+C to clear the current line.\n"
          "- Preload files with --input-file or by running the TUI.\n\n");
  fflush(stdout);
}

int readline_capture_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out) {
  (void) config;
  if (!output || !output_len) {
    rl_set_error(error_out, "internal: missing buffer pointers");
    return -1;
  }

  print_readline_banner();

  rl_catch_signals = 0;

  StringBuffer buffer;
  sb_init(&buffer);
  int interactive = isatty(STDIN_FILENO);

  while (1) {
    char *line = readline(interactive ? "DeepSeek MPI> " : NULL);
    if (!line) {
      break;
    }
    if (interactive) {
      fputs("\r\033[K", stdout);
      fflush(stdout);
    }
    if (strcmp(line, ".") == 0) {
      free(line);
      break;
    }
    if (*line != '\0') {
      add_history(line);
    }
    sb_append_str(&buffer, line);
    sb_append_char(&buffer, '\n');
    free(line);
  }

  if (buffer.length == 0) {
    sb_clean(&buffer);
    rl_set_error(error_out, "no readline payload captured");
    return -1;
  }

  size_t captured_len = buffer.length;
  char *result = sb_detach(&buffer);
  sb_clean(&buffer);
  if (!result) {
    rl_set_error(error_out, "unable to finalize readline buffer");
    return -1;
  }
  *output = result;
  *output_len = captured_len;
  return 0;
}
