#include "app_config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void cfg_assign_error(char **error_out, const char *fmt, ...) {
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

static bool matches(const char *value, const char *expected) {
  if (!value || !expected) {
    return false;
  }
  return strcmp(value, expected) == 0;
}

static int parse_size_value(const char *text, size_t *out) {
  if (!text || !out) {
    return -1;
  }
  errno = 0;
  char *end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return -1;
  }
  *out = (size_t) value;
  return 0;
}

static int parse_int_value(const char *text, int *out) {
  if (!text || !out) {
    return -1;
  }
  errno = 0;
  char *end = NULL;
  long value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return -1;
  }
  *out = (int) value;
  return 0;
}

static int parse_long_value(const char *text, long *out) {
  if (!text || !out) {
    return -1;
  }
  errno = 0;
  char *end = NULL;
  long value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') {
    return -1;
  }
  *out = value;
  return 0;
}

static int parse_bool_value(const char *text, bool *out) {
  if (!text || !out) {
    return -1;
  }
  if (strcasecmp(text, "1") == 0 || strcasecmp(text, "true") == 0 || strcasecmp(text, "yes") == 0 ||
      strcasecmp(text, "on") == 0 || strcasecmp(text, "enabled") == 0) {
    *out = true;
    return 0;
  }
  if (strcasecmp(text, "0") == 0 || strcasecmp(text, "false") == 0 || strcasecmp(text, "no") == 0 ||
      strcasecmp(text, "off") == 0 || strcasecmp(text, "disabled") == 0) {
    *out = false;
    return 0;
  }
  return -1;
}

ProgramConfig config_defaults(void) {
  ProgramConfig cfg;
  cfg.api_endpoint = cfg_strdup(DEEPSEEK_DEFAULT_ENDPOINT);
  cfg.api_key_env = cfg_strdup(DEEPSEEK_DEFAULT_API_ENV);
  cfg.explicit_api_key = NULL;
  cfg.log_file = cfg_strdup(DEEPSEEK_DEFAULT_LOG_FILE);
  cfg.input_file = NULL;
  cfg.input_text = NULL;
  cfg.config_file = NULL;
  cfg.response_dir = cfg_strdup(DEEPSEEK_DEFAULT_RESPONSE_DIR);
  cfg.model = NULL;
  cfg.anthropic_version = cfg_strdup(ANTHROPIC_DEFAULT_VERSION);
  cfg.target_tasks = 0;
  cfg.target_tasks_set = false;
  cfg.response_files_enabled = true;

  cfg.chunk_size = DEEPSEEK_DEFAULT_CHUNK_SIZE;
  cfg.max_request_bytes = DEEPSEEK_DEFAULT_MAX_REQUEST;
  cfg.max_retries = DEEPSEEK_DEFAULT_RETRIES;
  cfg.timeout_seconds = DEEPSEEK_DEFAULT_TIMEOUT_SECONDS;
  cfg.retry_delay_ms = DEEPSEEK_DEFAULT_RETRY_DELAY_MS;
  cfg.progress_interval = 1;
  cfg.verbosity = 1;
  cfg.network_retry_limit = DEEPSEEK_DEFAULT_NETWORK_RESETS;
  cfg.max_output_tokens = AI_DEFAULT_MAX_OUTPUT_TOKENS;

  cfg.show_progress = true;
  cfg.use_tui = true;
  cfg.dry_run = false;
  cfg.allow_file_prompt = true;
  cfg.use_stdin = false;
  cfg.force_quiet = false;

  cfg.rank = 0;
  cfg.world_size = 1;
  cfg.provider = API_PROVIDER_DEEPSEEK;
  cfg.auto_scale_mode = AUTOSCALE_MODE_NONE;
  cfg.auto_scale_threshold_bytes = DEEPSEEK_AUTOSCALE_DEFAULT_THRESHOLD;
  cfg.auto_scale_factor = DEEPSEEK_AUTOSCALE_DEFAULT_FACTOR;
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
  free(config->model);
  free(config->anthropic_version);
  config->api_endpoint = NULL;
  config->api_key_env = NULL;
  config->explicit_api_key = NULL;
  config->log_file = NULL;
  config->input_file = NULL;
  config->input_text = NULL;
  config->config_file = NULL;
  config->response_dir = NULL;
  config->response_files_enabled = true;
  config->model = NULL;
  config->anthropic_version = NULL;
  config->target_tasks = 0;
  config->target_tasks_set = false;
  config->max_output_tokens = AI_DEFAULT_MAX_OUTPUT_TOKENS;
  config->provider = API_PROVIDER_DEEPSEEK;
  config->auto_scale_mode = AUTOSCALE_MODE_NONE;
  config->auto_scale_threshold_bytes = DEEPSEEK_AUTOSCALE_DEFAULT_THRESHOLD;
  config->auto_scale_factor = DEEPSEEK_AUTOSCALE_DEFAULT_FACTOR;
}

void config_set_provider(ProgramConfig *config, ApiProvider provider) {
  if (!config) {
    return;
  }

  ApiProvider previous = config->provider;
  bool endpoint_default = (config->api_endpoint == NULL);
  bool env_default = (config->api_key_env == NULL);

  if (!endpoint_default) {
    if (previous == API_PROVIDER_DEEPSEEK && matches(config->api_endpoint, DEEPSEEK_DEFAULT_ENDPOINT)) {
      endpoint_default = true;
    } else if (previous == API_PROVIDER_OPENAI && matches(config->api_endpoint, OPENAI_DEFAULT_ENDPOINT)) {
      endpoint_default = true;
    } else if (previous == API_PROVIDER_ANTHROPIC && matches(config->api_endpoint, ANTHROPIC_DEFAULT_ENDPOINT)) {
      endpoint_default = true;
    }
  }

  if (!env_default) {
    if (previous == API_PROVIDER_DEEPSEEK && matches(config->api_key_env, DEEPSEEK_DEFAULT_API_ENV)) {
      env_default = true;
    } else if (previous == API_PROVIDER_OPENAI && matches(config->api_key_env, OPENAI_DEFAULT_API_ENV)) {
      env_default = true;
    } else if (previous == API_PROVIDER_ANTHROPIC && matches(config->api_key_env, ANTHROPIC_DEFAULT_API_ENV)) {
      env_default = true;
    }
  }

  config->provider = provider;

  if (endpoint_default) {
    switch (provider) {
    case API_PROVIDER_DEEPSEEK:
      config_replace_string(&config->api_endpoint, DEEPSEEK_DEFAULT_ENDPOINT);
      break;
    case API_PROVIDER_OPENAI:
      config_replace_string(&config->api_endpoint, OPENAI_DEFAULT_ENDPOINT);
      break;
    case API_PROVIDER_ANTHROPIC:
      config_replace_string(&config->api_endpoint, ANTHROPIC_DEFAULT_ENDPOINT);
      break;
    }
  }

  if (env_default) {
    switch (provider) {
    case API_PROVIDER_DEEPSEEK:
      config_replace_string(&config->api_key_env, DEEPSEEK_DEFAULT_API_ENV);
      break;
    case API_PROVIDER_OPENAI:
      config_replace_string(&config->api_key_env, OPENAI_DEFAULT_API_ENV);
      break;
    case API_PROVIDER_ANTHROPIC:
      config_replace_string(&config->api_key_env, ANTHROPIC_DEFAULT_API_ENV);
      break;
    }
  }

  if ((provider == API_PROVIDER_OPENAI || provider == API_PROVIDER_ANTHROPIC) && !config->model) {
    if (provider == API_PROVIDER_OPENAI) {
      config_replace_string(&config->model, OPENAI_DEFAULT_MODEL);
    } else {
      config_replace_string(&config->model, ANTHROPIC_DEFAULT_MODEL);
    }
  }

  if (provider == API_PROVIDER_ANTHROPIC && !config->anthropic_version) {
    config_replace_string(&config->anthropic_version, ANTHROPIC_DEFAULT_VERSION);
  }
}

int config_parse_provider(const char *text, ApiProvider *out) {
  if (!text || !out) {
    return -1;
  }
  if (strcasecmp(text, "deepseek") == 0) {
    *out = API_PROVIDER_DEEPSEEK;
  } else if (strcasecmp(text, "openai") == 0) {
    *out = API_PROVIDER_OPENAI;
  } else if (strcasecmp(text, "anthropic") == 0) {
    *out = API_PROVIDER_ANTHROPIC;
  } else {
    return -1;
  }
  return 0;
}

int config_parse_autoscale_mode(const char *text, AutoScaleMode *out) {
  if (!text || !out) {
    return -1;
  }
  if (strcasecmp(text, "none") == 0 || strcasecmp(text, "off") == 0) {
    *out = AUTOSCALE_MODE_NONE;
  } else if (strcasecmp(text, "threads") == 0 || strcasecmp(text, "ranks") == 0) {
    *out = AUTOSCALE_MODE_THREADS;
  } else if (strcasecmp(text, "chunks") == 0 || strcasecmp(text, "split") == 0 ||
             strcasecmp(text, "tasks") == 0) {
    *out = AUTOSCALE_MODE_CHUNKS;
  } else {
    return -1;
  }
  return 0;
}

int config_apply_kv(ProgramConfig *config, const char *key, const char *value, char **error_out) {
  if (!config || !key) {
    cfg_assign_error(error_out, "internal: missing config/key");
    return -1;
  }
  const char *val = value ? value : "";

  if (strcmp(key, "api_endpoint") == 0) {
    config_replace_string(&config->api_endpoint, val);
  } else if (strcmp(key, "api_key_env") == 0) {
    config_replace_string(&config->api_key_env, val);
  } else if (strcmp(key, "api_key") == 0) {
    config_replace_string(&config->explicit_api_key, val);
  } else if (strcmp(key, "log_file") == 0) {
    config_replace_string(&config->log_file, val);
  } else if (strcmp(key, "input_file") == 0) {
    config_replace_string(&config->input_file, val);
  } else if (strcmp(key, "inline_text") == 0) {
    config_replace_string(&config->input_text, val);
  } else if (strcmp(key, "response_dir") == 0) {
    config_replace_string(&config->response_dir, val);
  } else if (strcmp(key, "response_files") == 0) {
    bool enabled;
    if (parse_bool_value(val, &enabled) != 0) {
      cfg_assign_error(error_out, "invalid response_files value: %s", val);
      return -1;
    }
    config->response_files_enabled = enabled;
  } else if (strcmp(key, "model") == 0) {
    config_replace_string(&config->model, val);
  } else if (strcmp(key, "anthropic_version") == 0) {
    config_replace_string(&config->anthropic_version, val);
  } else if (strcmp(key, "chunk_size") == 0) {
    size_t tmp;
    if (parse_size_value(val, &tmp) != 0) {
      cfg_assign_error(error_out, "invalid chunk_size value: %s", val);
      return -1;
    }
    config->chunk_size = tmp;
  } else if (strcmp(key, "max_request_bytes") == 0) {
    size_t tmp;
    if (parse_size_value(val, &tmp) != 0) {
      cfg_assign_error(error_out, "invalid max_request_bytes: %s", val);
      return -1;
    }
    config->max_request_bytes = tmp;
  } else if (strcmp(key, "tasks") == 0) {
    size_t tmp;
    if (parse_size_value(val, &tmp) != 0 || tmp == 0) {
      cfg_assign_error(error_out, "invalid tasks value: %s", val);
      return -1;
    }
    config->target_tasks = tmp;
    config->target_tasks_set = true;
  } else if (strcmp(key, "max_retries") == 0) {
    int tmp;
    if (parse_int_value(val, &tmp) != 0) {
      cfg_assign_error(error_out, "invalid max_retries: %s", val);
      return -1;
    }
    config->max_retries = tmp;
  } else if (strcmp(key, "network_retries") == 0) {
    int tmp;
    if (parse_int_value(val, &tmp) != 0) {
      cfg_assign_error(error_out, "invalid network_retries: %s", val);
      return -1;
    }
    config->network_retry_limit = tmp;
  } else if (strcmp(key, "progress_interval") == 0) {
    int tmp;
    if (parse_int_value(val, &tmp) != 0 || tmp <= 0) {
      cfg_assign_error(error_out, "invalid progress_interval: %s", val);
      return -1;
    }
    config->progress_interval = tmp;
  } else if (strcmp(key, "verbosity") == 0) {
    int tmp;
    if (parse_int_value(val, &tmp) != 0 || tmp < 0) {
      cfg_assign_error(error_out, "invalid verbosity: %s", val);
      return -1;
    }
    config->verbosity = tmp;
  } else if (strcmp(key, "max_output_tokens") == 0) {
    int tmp;
    if (parse_int_value(val, &tmp) != 0 || tmp <= 0) {
      cfg_assign_error(error_out, "invalid max_output_tokens: %s", val);
      return -1;
    }
    config->max_output_tokens = tmp;
  } else if (strcmp(key, "timeout") == 0) {
    long tmp;
    if (parse_long_value(val, &tmp) != 0 || tmp <= 0) {
      cfg_assign_error(error_out, "invalid timeout: %s", val);
      return -1;
    }
    config->timeout_seconds = tmp;
  } else if (strcmp(key, "retry_delay_ms") == 0) {
    long tmp;
    if (parse_long_value(val, &tmp) != 0 || tmp < 0) {
      cfg_assign_error(error_out, "invalid retry_delay_ms: %s", val);
      return -1;
    }
    config->retry_delay_ms = tmp;
  } else if (strcmp(key, "dry_run") == 0) {
    bool flag;
    if (parse_bool_value(val, &flag) != 0) {
      cfg_assign_error(error_out, "invalid dry_run: %s", val);
      return -1;
    }
    config->dry_run = flag;
  } else if (strcmp(key, "show_progress") == 0) {
    bool flag;
    if (parse_bool_value(val, &flag) != 0) {
      cfg_assign_error(error_out, "invalid show_progress: %s", val);
      return -1;
    }
    config->show_progress = flag;
  } else if (strcmp(key, "use_tui") == 0 || strcmp(key, "tui") == 0) {
    bool flag;
    if (parse_bool_value(val, &flag) != 0) {
      cfg_assign_error(error_out, "invalid use_tui: %s", val);
      return -1;
    }
    config->use_tui = flag;
  } else if (strcmp(key, "allow_file_prompt") == 0) {
    bool flag;
    if (parse_bool_value(val, &flag) != 0) {
      cfg_assign_error(error_out, "invalid allow_file_prompt: %s", val);
      return -1;
    }
    config->allow_file_prompt = flag;
  } else if (strcmp(key, "use_stdin") == 0 || strcmp(key, "stdin") == 0) {
    bool flag;
    if (parse_bool_value(val, &flag) != 0) {
      cfg_assign_error(error_out, "invalid use_stdin: %s", val);
      return -1;
    }
    config->use_stdin = flag;
  } else if (strcmp(key, "force_quiet") == 0 || strcmp(key, "quiet") == 0) {
    bool flag;
    if (parse_bool_value(val, &flag) != 0) {
      cfg_assign_error(error_out, "invalid quiet: %s", val);
      return -1;
    }
    config->force_quiet = flag;
    if (flag) {
      config->verbosity = 0;
    }
  } else if (strcmp(key, "api_provider") == 0) {
    ApiProvider provider;
    if (config_parse_provider(val, &provider) != 0) {
      cfg_assign_error(error_out, "unknown api_provider: %s", val);
      return -1;
    }
    config_set_provider(config, provider);
  } else if (strcmp(key, "auto_scale_mode") == 0) {
    AutoScaleMode mode;
    if (config_parse_autoscale_mode(val, &mode) != 0) {
      cfg_assign_error(error_out, "unknown auto_scale_mode: %s", val);
      return -1;
    }
    config->auto_scale_mode = mode;
  } else if (strcmp(key, "auto_scale_threshold") == 0) {
    size_t tmp;
    if (parse_size_value(val, &tmp) != 0) {
      cfg_assign_error(error_out, "invalid auto_scale_threshold: %s", val);
      return -1;
    }
    config->auto_scale_threshold_bytes = tmp;
  } else if (strcmp(key, "auto_scale_factor") == 0) {
    int tmp;
    if (parse_int_value(val, &tmp) != 0 || tmp <= 0) {
      cfg_assign_error(error_out, "invalid auto_scale_factor: %s", val);
      return -1;
    }
    config->auto_scale_factor = tmp;
  } else {
    cfg_assign_error(error_out, "unknown config key: %s", key);
    return -1;
  }

  return 0;
}

void config_finalize(ProgramConfig *config) {
  if (!config) {
    return;
  }
  if (config->chunk_size < DEEPSEEK_MIN_CHUNK_SIZE) {
    config->chunk_size = DEEPSEEK_MIN_CHUNK_SIZE;
  }
  if (config->max_request_bytes < config->chunk_size) {
    config->max_request_bytes = config->chunk_size * 2;
  }
  if (config->max_retries < 0) {
    config->max_retries = 0;
  }
  if (config->timeout_seconds <= 0) {
    config->timeout_seconds = DEEPSEEK_DEFAULT_TIMEOUT_SECONDS;
  }
  if (config->retry_delay_ms < 0) {
    config->retry_delay_ms = DEEPSEEK_DEFAULT_RETRY_DELAY_MS;
  }
  if (config->progress_interval <= 0) {
    config->progress_interval = 1;
  }
  if (config->max_output_tokens <= 0) {
    config->max_output_tokens = AI_DEFAULT_MAX_OUTPUT_TOKENS;
  }
  if (config->verbosity < 0) {
    config->verbosity = 0;
  }
  if (config->force_quiet) {
    config->verbosity = 0;
  }
  if (config->network_retry_limit < 0) {
    config->network_retry_limit = 0;
  }
  if (config->auto_scale_factor < 1) {
    config->auto_scale_factor = DEEPSEEK_AUTOSCALE_DEFAULT_FACTOR;
  }
  if (config->auto_scale_threshold_bytes == 0) {
    config->auto_scale_threshold_bytes = DEEPSEEK_AUTOSCALE_DEFAULT_THRESHOLD;
  }
  if (config->auto_scale_mode < AUTOSCALE_MODE_NONE || config->auto_scale_mode > AUTOSCALE_MODE_CHUNKS) {
    config->auto_scale_mode = AUTOSCALE_MODE_NONE;
  }
}
