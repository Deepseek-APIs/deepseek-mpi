#ifndef TUI_H
#define TUI_H

#include "app_config.h"

int tui_capture_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out);

#endif /* TUI_H */
