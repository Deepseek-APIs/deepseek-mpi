#ifndef READLINE_PROMPT_H
#define READLINE_PROMPT_H

#include <stddef.h>

#include "app_config.h"

int readline_capture_payload(ProgramConfig *config, char **output, size_t *output_len, char **error_out);

#endif /* READLINE_PROMPT_H */
