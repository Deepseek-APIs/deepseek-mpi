#ifndef CLI_H
#define CLI_H

#include "app_config.h"

typedef enum {
  CLI_OK = 0,
  CLI_REQUEST_EXIT = 1,
  CLI_ERROR = -1
} CliResult;

CliResult cli_parse_args(int argc, char **argv, ProgramConfig *config);

#endif /* CLI_H */
