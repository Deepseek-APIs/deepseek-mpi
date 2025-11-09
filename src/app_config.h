#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "deepseek.h"

typedef enum {
  API_PROVIDER_DEEPSEEK = 0,
  API_PROVIDER_OPENAI,
  API_PROVIDER_ANTHROPIC,
  API_PROVIDER_ZAI
} ApiProvider;

typedef enum {
  AUTOSCALE_MODE_NONE = 0,
  AUTOSCALE_MODE_THREADS,
  AUTOSCALE_MODE_CHUNKS
} AutoScaleMode;

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
  char *model;
  char *anthropic_version;
  size_t target_tasks;
  bool target_tasks_set;
  bool response_files_enabled;
  char *payload_file;
  char *mpirun_cmd;
  int mpi_processes;

  size_t chunk_size;
  size_t max_request_bytes;
  int max_retries;
  long timeout_seconds;
  long retry_delay_ms;
  int progress_interval;
  int verbosity;
  int network_retry_limit;
  int max_output_tokens;

  bool show_progress;
  bool use_tui;
  bool noninteractive_mode;
  bool use_readline_prompt;
  bool use_tui_log_view;
  bool tui_log_view_explicit;
  bool dry_run;
  bool allow_file_prompt;
  bool use_stdin;
  bool force_quiet;
  bool repl_mode;

  int rank;
  int world_size;
  ApiProvider provider;
  bool provider_locked;
  AutoScaleMode auto_scale_mode;
  size_t auto_scale_threshold_bytes;
  int auto_scale_factor;
} ProgramConfig;

ProgramConfig config_defaults(void);
void config_free(ProgramConfig *config);
void config_replace_string(char **target, const char *value);
void config_record_rank(ProgramConfig *config, int rank, int world_size);
void config_set_provider(ProgramConfig *config, ApiProvider provider);
int config_apply_kv(ProgramConfig *config, const char *key, const char *value, char **error_out);
void config_finalize(ProgramConfig *config);
int config_parse_provider(const char *text, ApiProvider *out);
int config_parse_autoscale_mode(const char *text, AutoScaleMode *out);

#endif /* APP_CONFIG_H */
