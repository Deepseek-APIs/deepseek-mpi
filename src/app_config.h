#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "deepseek.h"

/**
 * Holds runtime configuration resolved from defaults, config files, env, and CLI flags.
 */
typedef struct {
  char *api_endpoint;
  char *api_key_env;
  char *explicit_api_key;
  char *log_file;
  char *input_file;
  char *input_text;
  char *config_file;
  char *response_dir;
  size_t target_tasks;
  bool target_tasks_set;

  size_t chunk_size;
  size_t max_request_bytes;
  int max_retries;
  long timeout_seconds;
  long retry_delay_ms;
  int progress_interval;
  int verbosity;

  bool show_progress;
  bool use_tui;
  bool dry_run;
  bool allow_file_prompt;
  bool use_stdin;
  bool force_quiet;

  int rank;
  int world_size;
} ProgramConfig;

ProgramConfig config_defaults(void);
void config_free(ProgramConfig *config);
void config_replace_string(char **target, const char *value);
void config_record_rank(ProgramConfig *config, int rank, int world_size);

#endif /* APP_CONFIG_H */
