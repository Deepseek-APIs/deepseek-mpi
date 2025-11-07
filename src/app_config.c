#include "app_config.h"

#include <stdlib.h>
#include <string.h>

static char *cfg_strdup(const char *value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len);
  return copy;
}

ProgramConfig config_defaults(void) {
  ProgramConfig cfg;
  cfg.api_endpoint = cfg_strdup(DEEPSEEK_DEFAULT_ENDPOINT);
  cfg.api_key_env = cfg_strdup("DEEPSEEK_API_KEY");
  cfg.explicit_api_key = NULL;
  cfg.log_file = cfg_strdup(DEEPSEEK_DEFAULT_LOG_FILE);
  cfg.input_file = NULL;
  cfg.input_text = NULL;
  cfg.config_file = NULL;
  cfg.response_dir = NULL;
  cfg.target_tasks = 0;
  cfg.target_tasks_set = false;

  cfg.chunk_size = DEEPSEEK_DEFAULT_CHUNK_SIZE;
  cfg.max_request_bytes = DEEPSEEK_DEFAULT_MAX_REQUEST;
  cfg.max_retries = DEEPSEEK_DEFAULT_RETRIES;
  cfg.timeout_seconds = DEEPSEEK_DEFAULT_TIMEOUT_SECONDS;
  cfg.retry_delay_ms = DEEPSEEK_DEFAULT_RETRY_DELAY_MS;
  cfg.progress_interval = 1;
  cfg.verbosity = 1;

  cfg.show_progress = true;
  cfg.use_tui = true;
  cfg.dry_run = false;
  cfg.allow_file_prompt = true;
  cfg.use_stdin = false;
  cfg.force_quiet = false;

  cfg.rank = 0;
  cfg.world_size = 1;
  return cfg;
}

void config_replace_string(char **target, const char *value) {
  if (!target) {
    return;
  }
  free(*target);
  *target = value ? cfg_strdup(value) : NULL;
}

void config_record_rank(ProgramConfig *config, int rank, int world_size) {
  if (!config) {
    return;
  }
  config->rank = rank;
  config->world_size = world_size;
}

void config_free(ProgramConfig *config) {
  if (!config) {
    return;
  }
  free(config->api_endpoint);
  free(config->api_key_env);
  free(config->explicit_api_key);
  free(config->log_file);
  free(config->input_file);
  free(config->input_text);
  free(config->config_file);
  free(config->response_dir);
  config->api_endpoint = NULL;
  config->api_key_env = NULL;
  config->explicit_api_key = NULL;
  config->log_file = NULL;
  config->input_file = NULL;
  config->input_text = NULL;
  config->config_file = NULL;
  config->response_dir = NULL;
  config->target_tasks = 0;
  config->target_tasks_set = false;
}
