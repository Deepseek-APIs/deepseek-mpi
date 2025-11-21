#include "cli.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "file_loader.h"
#include "string_buffer.h"

enum {
  OPT_CONFIG_FILE = 1000,
  OPT_MAX_REQUEST,
  OPT_NO_TUI,
  OPT_TUI,
  OPT_DRY_RUN,
  OPT_SHOW_PROGRESS,
  OPT_HIDE_PROGRESS,
  OPT_API_KEY_VALUE,
  OPT_VERSION,
  OPT_RESPONSE_DIR,
  OPT_RESPONSE_FILES_ON,
  OPT_RESPONSE_FILES_OFF,
  OPT_TASKS,
  OPT_SYSTEM_PROMPT,
  OPT_NP,
  OPT_MP,
  OPT_API_PROVIDER,
  OPT_MAX_OUTPUT_TOKENS,
  OPT_ANTHROPIC_VERSION,
  OPT_NETWORK_RETRIES,
  OPT_READLINE_ON,
  OPT_READLINE_OFF,
  OPT_AUTOSCALE_MODE,
  OPT_AUTOSCALE_THRESHOLD,
  OPT_AUTOSCALE_FACTOR,
  OPT_TUI_LOG_VIEW_ON,
  OPT_TUI_LOG_VIEW_OFF,
  OPT_REPL,
  OPT_NONINTERACTIVE,
  OPT_REPL_HISTORY_LIMIT
};

static void print_version(void) {
  printf("deepseek-mpi %s\n", deepseek_get_version());
}

static void print_help(const char *prog) {
  printf("Usage: %s [options]\n\n", prog);
  puts("Key options:\n"
       "  --api-endpoint URL         Override DeepSeek API endpoint\n"
       "  --api-key-env NAME         Environment variable containing API key\n"
       "  --api-key VALUE            Provide API key directly (overrides env)\n"
       "  --chunk-size BYTES         Chunk size per MPI slice\n"
       "  --max-request-bytes BYTES  Upper bound for encoded payload\n"
       "  --input-file PATH          Read payload from file (use '-' for stdin)\n"
       "  --stdin                    Force stdin for payload\n"
       "  --inline-text STRING       Provide inline text without TUI\n"
       "  --system-prompt FILE       Read a system prompt from FILE (sent with every request)\n"
       "  --config FILE              Load key=value defaults from file\n"
       "  --log-file PATH            Redirect log output\n"
       "  --response-dir DIR         Persist each chunk response as JSON\n"
       "  --response-files / --no-response-files  Toggle per-rank response file emission (default on)\n"
       "  --tasks N / --mp N / --np N  Desired task count (auto chunking across MPI ranks)\n"
       "  --auto-scale-threshold BYTES  Trigger size for automatic scaling (default 100MB)\n"
       "  --auto-scale-mode MODE      Autoscale strategy: none, threads, chunks\n"
       "  --auto-scale-factor N       Multiplier applied when autoscale fires\n"
       "  --api-provider NAME        Target API provider: deepseek, openai, anthropic, zai\n"
       "  --model MODEL              Override model for OpenAI/Anthropic/Zai-compatible APIs\n"
       "  --max-output-tokens N      Cap response tokens for OpenAI/Anthropic/Zai providers\n"
       "  --anthropic-version DATE   Override the x-anthropic-version header\n"
       "  --timeout SECONDS          HTTP timeout\n"
       "  --max-retries N            Retry count per chunk\n"
       "  --retry-delay-ms MS        Delay between retries in milliseconds\n"
       "  --network-retries N        MPI-level client resets after network failures\n"
       "  --readline / --no-readline  Toggle GNU Readline prompt when TUI is disabled\n"
       "  --repl                    Keep an interactive REPL session inside deepseek_mpi\n"
       "  --noninteractive          Disable TUI/readline and require --input-file plus inline text\n"
       "  --tui-log-view / --no-tui-log-view  Control the post-prompt curses log pane (auto-on with --tui)\n"
       "  --tui / --no-tui           Toggle ncurses interface\n"
       "  --dry-run                  Skip HTTP calls (for smoke tests)\n"
       "  --verbose / --quiet        Adjust console verbosity\n"
       "  --version                  Print version\n"
       "  --help                     This message\n");
  printf("  --repl-history N         Number of prior REPL turns to resend (0 = unlimited, default %llu)\n",
         (unsigned long long) DEEPSEEK_DEFAULT_REPL_HISTORY);
}

static int parse_size(const char *text, size_t *out) {
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

static int parse_int(const char *text, int *out) {
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

static int load_config_file(ProgramConfig *cfg, const char *path) {
  if (!cfg || !path) {
    return -1;
  }
  char *contents = NULL;
  size_t len = 0;
  char *error = NULL;
  if (file_loader_read_all(path, &contents, &len, &error) != 0) {
    fprintf(stderr, "Failed to read config %s: %s\n", path, error ? error : "unknown error");
    free(error);
    return -1;
  }

  config_replace_string(&cfg->config_file, path);

  char *line = contents;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next) {
      *next = '\0';
    }
    if (*line && *line != '#') {
      char *eq = strchr(line, '=');
      if (eq) {
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;
        char *kv_error = NULL;
        if (config_apply_kv(cfg, key, value, &kv_error) != 0) {
          fprintf(stderr, "Invalid config entry %s=%s: %s\n", key, value, kv_error ? kv_error : "unknown");
        }
        free(kv_error);
      }
    }
    if (!next) {
      break;
    }
    line = next + 1;
  }

  free(contents);
  return 0;
}

static void capture_trailing_payload(int argc, char **argv, ProgramConfig *cfg) {
  if (!cfg) {
    return;
  }
  if (optind >= argc) {
    return;
  }
  StringBuffer buffer;
  sb_init(&buffer);
  for (int i = optind; i < argc; ++i) {
    if (i > optind) {
      sb_append_char(&buffer, ' ');
    }
    sb_append_str(&buffer, argv[i]);
  }
  config_replace_string(&cfg->input_text, buffer.data);
  sb_clean(&buffer);
}

CliResult cli_parse_args(int argc, char **argv, ProgramConfig *config) {
  if (!config) {
    return CLI_ERROR;
  }

  static const struct option long_opts[] = {
      {"api-endpoint", required_argument, NULL, 'e'},
      {"api-key-env", required_argument, NULL, 'k'},
      {"api-key", required_argument, NULL, OPT_API_KEY_VALUE},
      {"api-provider", required_argument, NULL, OPT_API_PROVIDER},
      {"chunk-size", required_argument, NULL, 'c'},
      {"log-file", required_argument, NULL, 'l'},
      {"input-file", required_argument, NULL, 'f'},
      {"upload", required_argument, NULL, 'f'},
      {"timeout", required_argument, NULL, 't'},
      {"max-retries", required_argument, NULL, 'r'},
      {"retry-delay-ms", required_argument, NULL, 'd'},
      {"network-retries", required_argument, NULL, OPT_NETWORK_RETRIES},
      {"progress-interval", required_argument, NULL, 'p'},
      {"max-request-bytes", required_argument, NULL, OPT_MAX_REQUEST},
      {"config", required_argument, NULL, OPT_CONFIG_FILE},
      {"inline-text", required_argument, NULL, 'T'},
      {"model", required_argument, NULL, 'm'},
      {"max-output-tokens", required_argument, NULL, OPT_MAX_OUTPUT_TOKENS},
      {"anthropic-version", required_argument, NULL, OPT_ANTHROPIC_VERSION},
      {"response-dir", required_argument, NULL, OPT_RESPONSE_DIR},
      {"response-files", no_argument, NULL, OPT_RESPONSE_FILES_ON},
      {"no-response-files", no_argument, NULL, OPT_RESPONSE_FILES_OFF},
      {"system-prompt", required_argument, NULL, OPT_SYSTEM_PROMPT},
      {"tui-log-view", no_argument, NULL, OPT_TUI_LOG_VIEW_ON},
      {"no-tui-log-view", no_argument, NULL, OPT_TUI_LOG_VIEW_OFF},
      {"tasks", required_argument, NULL, OPT_TASKS},
      {"np", required_argument, NULL, OPT_NP},
      {"mp", required_argument, NULL, OPT_MP},
      {"auto-scale-mode", required_argument, NULL, OPT_AUTOSCALE_MODE},
      {"auto-scale-threshold", required_argument, NULL, OPT_AUTOSCALE_THRESHOLD},
      {"auto-scale-factor", required_argument, NULL, OPT_AUTOSCALE_FACTOR},
      {"stdin", no_argument, NULL, 'S'},
      {"readline", no_argument, NULL, OPT_READLINE_ON},
      {"no-readline", no_argument, NULL, OPT_READLINE_OFF},
      {"repl", no_argument, NULL, OPT_REPL},
      {"repl-history", required_argument, NULL, OPT_REPL_HISTORY_LIMIT},
      {"noninteractive", no_argument, NULL, OPT_NONINTERACTIVE},
      {"tui", no_argument, NULL, OPT_TUI},
      {"no-tui", no_argument, NULL, OPT_NO_TUI},
      {"dry-run", no_argument, NULL, OPT_DRY_RUN},
      {"show-progress", no_argument, NULL, OPT_SHOW_PROGRESS},
      {"hide-progress", no_argument, NULL, OPT_HIDE_PROGRESS},
      {"verbose", no_argument, NULL, 'v'},
      {"quiet", no_argument, NULL, 'q'},
      {"version", no_argument, NULL, OPT_VERSION},
      {"help", no_argument, NULL, 'h'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "e:k:c:l:f:t:r:d:p:m:T:S:qvh", long_opts, NULL)) != -1) {
    switch (opt) {
    case 'e':
      config_replace_string(&config->api_endpoint, optarg);
      break;
    case 'k':
      config_replace_string(&config->api_key_env, optarg);
      break;
    case 'c': {
      size_t value;
      if (parse_size(optarg, &value) != 0) {
        fprintf(stderr, "Invalid chunk size: %s\n", optarg);
        return CLI_ERROR;
      }
      config->chunk_size = value;
      break;
    }
    case 'l':
      config_replace_string(&config->log_file, optarg);
      break;
    case 'f':
      config_replace_string(&config->input_file, optarg);
      break;
    case 't': {
      long value;
      if (parse_long_value(optarg, &value) != 0) {
        fprintf(stderr, "Invalid timeout: %s\n", optarg);
        return CLI_ERROR;
      }
      config->timeout_seconds = value;
      break;
    }
    case 'r': {
      int value;
      if (parse_int(optarg, &value) != 0) {
        fprintf(stderr, "Invalid retry count: %s\n", optarg);
        return CLI_ERROR;
      }
      config->max_retries = value;
      break;
    }
    case 'd': {
      long value;
      if (parse_long_value(optarg, &value) != 0) {
        fprintf(stderr, "Invalid retry delay: %s\n", optarg);
        return CLI_ERROR;
      }
      config->retry_delay_ms = value;
      break;
    }
    case OPT_NETWORK_RETRIES: {
      int value;
      if (parse_int(optarg, &value) != 0) {
        fprintf(stderr, "Invalid network retries: %s\n", optarg);
        return CLI_ERROR;
      }
      config->network_retry_limit = value;
      break;
    }
    case 'p': {
      int value;
      if (parse_int(optarg, &value) != 0 || value <= 0) {
        fprintf(stderr, "Invalid progress interval: %s\n", optarg);
        return CLI_ERROR;
      }
      config->progress_interval = value;
      break;
    }
    case 'm':
      config_replace_string(&config->model, optarg);
      break;
    case 'T':
      config_replace_string(&config->input_text, optarg);
      config->use_tui = false;
      break;
    case OPT_RESPONSE_DIR:
      config_replace_string(&config->response_dir, optarg);
      break;
    case OPT_SYSTEM_PROMPT: {
      char *contents = NULL;
      size_t len = 0;
      char *error = NULL;
      if (file_loader_read_all(optarg, &contents, &len, &error) != 0) {
        fprintf(stderr, "Failed to read system prompt %s: %s\n", optarg, error ? error : "unknown error");
        free(error);
        return CLI_ERROR;
      }
      while (len > 0 && (contents[len - 1] == '\n' || contents[len - 1] == '\r')) {
        contents[--len] = '\0';
      }
      config_replace_string(&config->system_prompt, contents);
      free(contents);
      break;
    }
    case OPT_RESPONSE_FILES_ON:
      config->response_files_enabled = true;
      break;
    case OPT_RESPONSE_FILES_OFF:
      config->response_files_enabled = false;
      break;
    case OPT_TASKS:
    case OPT_NP:
    case OPT_MP: {
      size_t value;
      if (parse_size(optarg, &value) != 0 || value == 0) {
        fprintf(stderr, "Invalid task count: %s\n", optarg);
        return CLI_ERROR;
      }
      config->target_tasks = value;
      config->target_tasks_set = true;
      break;
    }
    case OPT_API_PROVIDER: {
      ApiProvider provider;
      if (config_parse_provider(optarg, &provider) != 0) {
        fprintf(stderr, "Invalid api provider: %s\n", optarg);
        return CLI_ERROR;
      }
      config_set_provider(config, provider);
      break;
    }
    case 'S':
      config->use_stdin = true;
      break;
    case OPT_READLINE_ON:
      config->use_readline_prompt = true;
      break;
    case OPT_READLINE_OFF:
      config->use_readline_prompt = false;
      break;
    case OPT_REPL:
      config->repl_mode = true;
      break;
    case OPT_REPL_HISTORY_LIMIT: {
      size_t value;
      if (parse_size(optarg, &value) != 0) {
        fprintf(stderr, "Invalid repl history size: %s\n", optarg);
        return CLI_ERROR;
      }
      config->repl_history_limit = value;
      break;
    }
    case OPT_NONINTERACTIVE:
      config->noninteractive_mode = true;
      config->use_tui = false;
      config->use_readline_prompt = false;
      break;
    case OPT_AUTOSCALE_MODE: {
      AutoScaleMode mode;
      if (config_parse_autoscale_mode(optarg, &mode) != 0) {
        fprintf(stderr, "Invalid auto-scale mode: %s\n", optarg);
        return CLI_ERROR;
      }
      config->auto_scale_mode = mode;
      break;
    }
    case OPT_AUTOSCALE_THRESHOLD: {
      size_t value;
      if (parse_size(optarg, &value) != 0) {
        fprintf(stderr, "Invalid auto-scale threshold: %s\n", optarg);
        return CLI_ERROR;
      }
      config->auto_scale_threshold_bytes = value;
      break;
    }
    case OPT_AUTOSCALE_FACTOR: {
      int value;
      if (parse_int(optarg, &value) != 0 || value <= 0) {
        fprintf(stderr, "Invalid auto-scale factor: %s\n", optarg);
        return CLI_ERROR;
      }
      config->auto_scale_factor = value;
      break;
    }
    case 'q':
      config->force_quiet = true;
      config->verbosity = 0;
      break;
    case 'v':
      config->verbosity++;
      break;
    case 'h':
      print_help(argv[0]);
      return CLI_REQUEST_EXIT;
    case OPT_API_KEY_VALUE:
      config_replace_string(&config->explicit_api_key, optarg);
      break;
    case OPT_MAX_REQUEST: {
      size_t value;
      if (parse_size(optarg, &value) != 0) {
        fprintf(stderr, "Invalid max request bytes: %s\n", optarg);
        return CLI_ERROR;
      }
      config->max_request_bytes = value;
      break;
    }
    case OPT_MAX_OUTPUT_TOKENS: {
      int value;
      if (parse_int(optarg, &value) != 0 || value <= 0) {
        fprintf(stderr, "Invalid max output tokens: %s\n", optarg);
        return CLI_ERROR;
      }
      config->max_output_tokens = value;
      break;
    }
    case OPT_ANTHROPIC_VERSION:
      config_replace_string(&config->anthropic_version, optarg);
      break;
    case OPT_CONFIG_FILE:
      if (load_config_file(config, optarg) != 0) {
        return CLI_ERROR;
      }
      break;
    case OPT_NO_TUI:
      config->use_tui = false;
      break;
    case OPT_TUI:
      config->use_tui = true;
      break;
    case OPT_DRY_RUN:
      config->dry_run = true;
      break;
    case OPT_TUI_LOG_VIEW_ON:
      config->use_tui_log_view = true;
      config->tui_log_view_explicit = true;
      break;
    case OPT_TUI_LOG_VIEW_OFF:
      config->use_tui_log_view = false;
      config->tui_log_view_explicit = true;
      break;
    case OPT_SHOW_PROGRESS:
      config->show_progress = true;
      break;
    case OPT_HIDE_PROGRESS:
      config->show_progress = false;
      break;
    case OPT_VERSION:
      print_version();
      return CLI_REQUEST_EXIT;
    default:
      print_help(argv[0]);
      return CLI_ERROR;
    }
  }

  capture_trailing_payload(argc, argv, config);

  config_finalize(config);

  if (config->noninteractive_mode) {
    if (!config->input_file || config->input_file[0] == '\0') {
      fprintf(stderr, "--noninteractive requires --input-file PATH to be specified.\n");
      return CLI_ERROR;
    }
    if (!config->input_text || config->input_text[0] == '\0') {
      fprintf(stderr, "--noninteractive requires an inline prompt. Use --inline-text or provide trailing arguments.\n");
      return CLI_ERROR;
    }
  }
  return CLI_OK;
}
