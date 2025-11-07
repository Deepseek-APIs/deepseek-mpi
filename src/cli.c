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
  OPT_RESPONSE_DIR
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
       "  --config FILE              Load key=value defaults from file\n"
       "  --log-file PATH            Redirect log output\n"
       "  --response-dir DIR         Persist each chunk response as JSON\n"
       "  --timeout SECONDS          HTTP timeout\n"
       "  --max-retries N            Retry count per chunk\n"
       "  --retry-delay-ms MS        Delay between retries in milliseconds\n"
       "  --tui / --no-tui           Toggle ncurses interface\n"
       "  --dry-run                  Skip HTTP calls (for smoke tests)\n"
       "  --verbose / --quiet        Adjust console verbosity\n"
       "  --version                  Print version\n"
       "  --help                     This message\n");
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
        if (strcmp(key, "api_endpoint") == 0) {
          config_replace_string(&cfg->api_endpoint, value);
        } else if (strcmp(key, "api_key_env") == 0) {
          config_replace_string(&cfg->api_key_env, value);
        } else if (strcmp(key, "chunk_size") == 0) {
          size_t tmp;
          if (parse_size(value, &tmp) == 0) {
            cfg->chunk_size = tmp;
          }
        } else if (strcmp(key, "max_retries") == 0) {
          int tmp;
          if (parse_int(value, &tmp) == 0) {
            cfg->max_retries = tmp;
          }
        } else if (strcmp(key, "timeout") == 0) {
          long tmp;
          if (parse_long_value(value, &tmp) == 0) {
            cfg->timeout_seconds = tmp;
          }
        } else if (strcmp(key, "log_file") == 0) {
          config_replace_string(&cfg->log_file, value);
        } else if (strcmp(key, "max_request_bytes") == 0) {
          size_t tmp;
          if (parse_size(value, &tmp) == 0) {
            cfg->max_request_bytes = tmp;
          }
        } else if (strcmp(key, "dry_run") == 0) {
          cfg->dry_run = (strcmp(value, "0") != 0 && strcasecmp(value, "false") != 0);
        } else if (strcmp(key, "response_dir") == 0) {
          config_replace_string(&cfg->response_dir, value);
        }
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
      {"chunk-size", required_argument, NULL, 'c'},
      {"log-file", required_argument, NULL, 'l'},
      {"input-file", required_argument, NULL, 'f'},
      {"upload", required_argument, NULL, 'f'},
      {"timeout", required_argument, NULL, 't'},
      {"max-retries", required_argument, NULL, 'r'},
      {"retry-delay-ms", required_argument, NULL, 'd'},
      {"progress-interval", required_argument, NULL, 'p'},
      {"max-request-bytes", required_argument, NULL, OPT_MAX_REQUEST},
      {"config", required_argument, NULL, OPT_CONFIG_FILE},
      {"inline-text", required_argument, NULL, 'T'},
      {"response-dir", required_argument, NULL, OPT_RESPONSE_DIR},
      {"stdin", no_argument, NULL, 'S'},
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
  while ((opt = getopt_long(argc, argv, "e:k:c:l:f:t:r:d:p:T:S:qvh", long_opts, NULL)) != -1) {
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
    case 'p': {
      int value;
      if (parse_int(optarg, &value) != 0 || value <= 0) {
        fprintf(stderr, "Invalid progress interval: %s\n", optarg);
        return CLI_ERROR;
      }
      config->progress_interval = value;
      break;
    }
    case 'T':
      config_replace_string(&config->input_text, optarg);
      config->use_tui = false;
      break;
    case OPT_RESPONSE_DIR:
      config_replace_string(&config->response_dir, optarg);
      break;
    case 'S':
      config->use_stdin = true;
      break;
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
  return CLI_OK;
}
