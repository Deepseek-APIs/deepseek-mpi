#ifndef TUI_H
#define TUI_H

#include "app_config.h"
#include "logger.h"

int tui_capture_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out);
int tui_capture_repl_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out);
int tui_log_view_start(void);
void tui_log_view_stop(void);
void tui_logger_sink(LoggerLevel level, int process_rank, const char *timestamp, const char *message, void *unused);
void tui_log_set_quiet(bool quiet);
bool tui_repl_attach_logger(Logger *logger);
void tui_repl_append_assistant(size_t turn, const char *text, size_t len);
void tui_repl_shutdown(void);

#endif /* TUI_H */
