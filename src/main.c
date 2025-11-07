#include <mpi.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "api_client.h"
#include "app_config.h"
#include "cli.h"
#include "deepseek.h"
#include "file_loader.h"
#include "input_chunker.h"
#include "logger.h"
#include "string_buffer.h"
#include "readline_prompt.h"
#include "tui.h"

typedef struct {
  char *data;
  size_t length;
} Payload;

static void maybe_adjust_chunk_from_tasks(ProgramConfig *config, const Payload *payload, Logger *logger);
static void maybe_autoscale_payload(ProgramConfig *config, const Payload *payload, Logger *logger);

static void assign_error(char **error_out, const char *fmt, ...) {
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
  size_t size = (size_t) needed + 1;
  char *msg = malloc(size);
  if (!msg) {
    va_end(args);
    return;
  }
  vsnprintf(msg, size, fmt, args);
  va_end(args);
  *error_out = msg;
}

static int load_from_stream(FILE *stream, Payload *payload, char **error_out) {
  if (file_loader_read_stream(stream, &payload->data, &payload->length, error_out) != 0) {
    return -1;
  }
  return 0;
}

static int duplicate_text(const char *text, Payload *payload, char **error_out) {
  if (!text) {
    assign_error(error_out, "no inline text available");
    return -1;
  }
  payload->length = strlen(text);
  payload->data = malloc(payload->length + 1);
  if (!payload->data) {
    assign_error(error_out, "unable to allocate payload copy");
    return -1;
  }
  memcpy(payload->data, text, payload->length + 1);
  return 0;
}

static int gather_payload_root(ProgramConfig *config, Logger *logger, Payload *payload) {
  if (!config || !payload) {
    return -1;
  }
  char *error = NULL;
  int rc = -1;

  if (config->input_file) {
    if (strcmp(config->input_file, "-") == 0) {
      logger_log(logger, LOG_LEVEL_INFO, "Reading payload from stdin (-)");
      rc = load_from_stream(stdin, payload, &error);
    } else {
      logger_log(logger, LOG_LEVEL_INFO, "Reading payload from file %s", config->input_file);
      rc = file_loader_read_all(config->input_file, &payload->data, &payload->length, &error);
    }
  } else if (config->use_stdin) {
    logger_log(logger, LOG_LEVEL_INFO, "Reading payload from stdin (flag)");
    rc = load_from_stream(stdin, payload, &error);
  } else if (config->input_text) {
    logger_log(logger, LOG_LEVEL_INFO, "Using inline text payload");
    rc = duplicate_text(config->input_text, payload, &error);
  } else if (config->use_tui) {
    logger_log(logger, LOG_LEVEL_INFO, "Launching ncurses TUI to capture payload");
    rc = tui_capture_payload(config, &payload->data, &payload->length, &error);
  } else if (config->use_readline_prompt) {
    logger_log(logger, LOG_LEVEL_INFO, "Launching GNU Readline prompt to capture payload");
    rc = readline_capture_payload(config, &payload->data, &payload->length, &error);
  } else {
    assign_error(&error, "No input source selected. Provide --input-file, --stdin, --inline-text, or enable the TUI.");
    rc = -1;
  }

  if (rc != 0) {
    logger_log(logger, LOG_LEVEL_ERROR, "Payload capture failed: %s", error ? error : "unknown error");
    free(error);
    return -1;
  }

  if (!payload->data || payload->length == 0) {
    logger_log(logger, LOG_LEVEL_ERROR, "Payload is empty");
    free(payload->data);
    payload->data = NULL;
    payload->length = 0;
    return -1;
  }

  logger_log(logger, LOG_LEVEL_INFO, "Captured %zu bytes of payload", payload->length);
  maybe_autoscale_payload(config, payload, logger);
  maybe_adjust_chunk_from_tasks(config, payload, logger);
  return 0;
}

static int broadcast_payload(char *buffer, size_t length) {
  if (!buffer && length > 0) {
    return -1;
  }
  size_t offset = 0;
  while (offset < length) {
    size_t remaining = length - offset;
    int chunk = remaining > (size_t) INT_MAX ? INT_MAX : (int) remaining;
    MPI_Bcast(buffer + offset, chunk, MPI_CHAR, 0, MPI_COMM_WORLD);
    offset += (size_t) chunk;
  }
  return 0;
}

static void maybe_adjust_chunk_from_tasks(ProgramConfig *config, const Payload *payload, Logger *logger) {
  if (!config || !payload || !logger) {
    return;
  }
  if (!config->target_tasks_set || config->target_tasks == 0 || payload->length == 0) {
    return;
  }
  size_t tasks = config->target_tasks;
  size_t chunk = payload->length / tasks;
  if (payload->length % tasks != 0) {
    chunk += 1;
  }
  if (chunk < DEEPSEEK_MIN_CHUNK_SIZE) {
    chunk = DEEPSEEK_MIN_CHUNK_SIZE;
  }
  if (chunk > payload->length) {
    chunk = payload->length;
  }
  config->chunk_size = chunk;
  if (config->max_request_bytes < chunk) {
    config->max_request_bytes = chunk;
  }
  logger_log(logger, LOG_LEVEL_INFO,
             "Auto chunking %zu-byte payload into %zu tasks (chunk size %zu bytes)", payload->length,
             tasks, chunk);
}

static void maybe_autoscale_payload(ProgramConfig *config, const Payload *payload, Logger *logger) {
  if (!config || !payload || !logger) {
    return;
  }
  if (config->auto_scale_mode == AUTOSCALE_MODE_NONE) {
    return;
  }
  if (config->auto_scale_threshold_bytes == 0 || config->auto_scale_factor <= 0) {
    return;
  }
  if (payload->length < config->auto_scale_threshold_bytes) {
    return;
  }
  if (config->auto_scale_mode == AUTOSCALE_MODE_CHUNKS) {
    size_t base = config->target_tasks_set && config->target_tasks > 0 ? config->target_tasks : (size_t) config->world_size;
    if (base == 0) {
      base = 1;
    }
    size_t scaled = base * (size_t) config->auto_scale_factor;
    if (scaled == 0) {
      scaled = base;
    }
    config->target_tasks = scaled;
    config->target_tasks_set = true;
    logger_log(logger, LOG_LEVEL_INFO,
               "Autoscale (chunks) triggered: payload %zu bytes >= %zu bytes -> %zu tasks (factor %d)",
               payload->length, config->auto_scale_threshold_bytes, scaled, config->auto_scale_factor);
  } else if (config->auto_scale_mode == AUTOSCALE_MODE_THREADS) {
    logger_log(logger, LOG_LEVEL_INFO,
               "Autoscale (threads) requested for %zu-byte payload but MPI world size is fixed at %d. "
               "Rerun with a higher -np or enable wrapper autoscale for rank scaling.",
               payload->length, config->world_size);
  }
}

static int ensure_directory(const char *path) {
  if (!path || *path == '\0') {
    errno = EINVAL;
    return -1;
  }
  char *mutable = strdup(path);
  if (!mutable) {
    return -1;
  }
  size_t len = strlen(mutable);
  while (len > 1 && mutable[len - 1] == '/') {
    mutable[len - 1] = '\0';
    len--;
  }
  for (char *cursor = mutable + 1; *cursor; ++cursor) {
    if (*cursor == '/') {
      *cursor = '\0';
      if (strlen(mutable) > 0) {
        if (mkdir(mutable, 0755) != 0 && errno != EEXIST) {
          int err = errno;
          free(mutable);
          errno = err;
          return -1;
        }
      }
      *cursor = '/';
    }
  }
  if (mkdir(mutable, 0755) != 0 && errno != EEXIST) {
    int err = errno;
    free(mutable);
    errno = err;
    return -1;
  }
  free(mutable);
  return 0;
}

static void persist_response_to_disk(const ProgramConfig *config, Logger *logger, size_t chunk_index,
                                     const StringBuffer *response) {
  if (!config || !logger || !config->response_files_enabled || !config->response_dir || !response ||
      response->length == 0) {
    return;
  }
  if (ensure_directory(config->response_dir) != 0) {
    logger_log(logger, LOG_LEVEL_WARN, "Rank %d unable to prepare response dir %s: %s", config->rank,
               config->response_dir, strerror(errno));
    return;
  }
  size_t dir_len = strlen(config->response_dir);
  const char *suffix = dir_len > 0 && config->response_dir[dir_len - 1] == '/' ? "" : "/";
  size_t needed = dir_len + strlen(suffix) + 64;
  char *path = malloc(needed);
  if (!path) {
    logger_log(logger, LOG_LEVEL_WARN, "Rank %d unable to allocate response path", config->rank);
    return;
  }
  int written = snprintf(path, needed, "%s%schunk-%06zu-r%d.json", config->response_dir, suffix, chunk_index,
                         config->rank);
  if (written < 0 || (size_t) written >= needed) {
    logger_log(logger, LOG_LEVEL_WARN, "Rank %d response path truncated", config->rank);
    free(path);
    return;
  }
  FILE *fp = fopen(path, "w");
  if (!fp) {
    logger_log(logger, LOG_LEVEL_WARN, "Rank %d cannot open %s: %s", config->rank, path, strerror(errno));
    free(path);
    return;
  }
  fwrite(response->data, 1, response->length, fp);
  fputc('\n', fp);
  fclose(fp);
  logger_log(logger, LOG_LEVEL_DEBUG, "Persisted response for chunk %zu to %s", chunk_index, path);
  free(path);
}

static void log_response_preview(const ProgramConfig *config, Logger *logger, size_t chunk_index,
                                 const StringBuffer *response) {
  (void) config;
  if (!logger || !response || response->length == 0) {
    return;
  }
  const size_t preview_limit = 4096;
  size_t slice = response->length > preview_limit ? preview_limit : response->length;
  logger_log(logger, LOG_LEVEL_INFO,
             "Chunk %zu response (%zu bytes)%s:\n%.*s",
             chunk_index,
             response->length,
             response->length > preview_limit ? " [preview]" : "",
             (int) slice,
             response->data ? response->data : "");
  if (response->length > preview_limit) {
    logger_log(logger, LOG_LEVEL_INFO, "... [truncated, see --response-dir for full payload]");
  }
}

static void process_chunks(const ProgramConfig *config, Logger *logger, const Payload *payload) {
  if (!config || !payload) {
    return;
  }
  ChunkCursor cursor;
  chunk_cursor_init(&cursor, config->chunk_size, payload->length, config->rank, config->world_size);

  ApiClient client;
  char *client_error = NULL;
  bool client_ready = (api_client_init(&client, config, &client_error) == 0);
  if (!client_ready) {
    logger_log(logger, LOG_LEVEL_ERROR, "API client init failed: %s", client_error ? client_error : "unknown");
    free(client_error);
  }

  StringBuffer response;
  bool response_ready = false;
  if (client_ready) {
    sb_init(&response);
    response_ready = true;
  }

  size_t processed = 0;
  size_t failures = 0;
  size_t network_failures = 0;
  size_t chunk_index = 0;
  size_t start = 0;
  size_t end = 0;
  bool aborted = false;

  while (client_ready && !aborted && chunk_cursor_next(&cursor, &start, &end, &chunk_index)) {
    const char *chunk_ptr = payload->data + start;
    size_t chunk_len = end - start;
    int remaining_resets = config->network_retry_limit;
    if (remaining_resets < 0) {
      remaining_resets = 0;
    }
    bool chunk_done = false;
    while (client_ready && !chunk_done) {
      char *error = NULL;
      ApiClientError api_error = API_CLIENT_ERROR_NONE;
      int api_rc = api_client_send(&client, chunk_ptr, chunk_len, chunk_index, response_ready ? &response : NULL,
                                   &error, &api_error);
      if (api_rc == 0) {
        logger_log(logger, LOG_LEVEL_INFO, "Chunk %zu (%zu bytes) succeeded", chunk_index, chunk_len);
        if (response_ready) {
          persist_response_to_disk(config, logger, chunk_index, &response);
          log_response_preview(config, logger, chunk_index, &response);
        }
        chunk_done = true;
        free(error);
        error = NULL;
      } else if (api_error == API_CLIENT_ERROR_NETWORK && remaining_resets > 0) {
        logger_log(logger, LOG_LEVEL_WARN,
                   "Chunk %zu network error: %s (resetting client, %d retries left)", chunk_index,
                   error ? error : "unknown error", remaining_resets);
        free(error);
        error = NULL;
        remaining_resets--;
        api_client_cleanup(&client);
        client_ready = false;
        char *reset_error = NULL;
        if (api_client_init(&client, config, &reset_error) != 0) {
          logger_log(logger, LOG_LEVEL_ERROR, "Unable to reinitialize API client: %s",
                     reset_error ? reset_error : "unknown error");
          free(reset_error);
          aborted = true;
          break;
        }
        client_ready = true;
        continue;
      } else {
        logger_log(logger, LOG_LEVEL_ERROR, "Chunk %zu failed: %s", chunk_index,
                   error ? error : "unknown error");
        if (api_error == API_CLIENT_ERROR_NETWORK) {
          network_failures++;
        }
        failures++;
        chunk_done = true;
        free(error);
        error = NULL;
      }

      if (!client_ready || aborted) {
        break;
      }
    }

    if (!client_ready || aborted) {
      break;
    }

    processed++;
    if (config->show_progress && config->progress_interval > 0 &&
        (processed % (size_t) config->progress_interval == 0)) {
      logger_log(logger, LOG_LEVEL_INFO, "Progress: %zu chunks processed on rank %d", processed, config->rank);
    }
  }

  unsigned long long stats[3] = {processed, failures, network_failures};
  unsigned long long global_stats[3] = {0, 0, 0};
  MPI_Reduce(stats, global_stats, 3, MPI_UNSIGNED_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

  if (config->rank == 0) {
    logger_log(logger, LOG_LEVEL_INFO,
               "Cluster summary: processed=%llu, failures=%llu, network_failures=%llu",
               global_stats[0], global_stats[1], global_stats[2]);
  }

  if (response_ready) {
    sb_clean(&response);
  }
  if (client_ready) {
    api_client_cleanup(&client);
  }
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int rank = 0;
  int world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  ProgramConfig config = config_defaults();
  config_record_rank(&config, rank, world_size);

  CliResult cli = cli_parse_args(argc, argv, &config);
  if (cli == CLI_ERROR) {
    MPI_Finalize();
    config_free(&config);
    return EXIT_FAILURE;
  }
  if (cli == CLI_REQUEST_EXIT) {
    MPI_Finalize();
    config_free(&config);
    return EXIT_SUCCESS;
  }

  Logger logger;
  if (logger_init(&logger, config.log_file, rank, config.verbosity) != 0) {
    fprintf(stderr, "Rank %d: unable to open log file %s, proceeding with stdout only\n", rank,
            config.log_file ? config.log_file : "(null)");
    logger.process_rank = rank;
    logger.verbosity = config.verbosity;
    logger.mirror_stdout = true;
    logger.handle = NULL;
  } else {
    logger_log(&logger, LOG_LEVEL_INFO, "deepseek-mpi %s starting on rank %d/%d", deepseek_get_version(), rank,
               world_size);
  }

  Payload payload = {0};
  int ready = 0;
  if (rank == 0) {
    ready = (gather_payload_root(&config, &logger, &payload) == 0) ? 1 : 0;
  }

  MPI_Bcast(&ready, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (!ready) {
    if (rank == 0 && payload.data) {
      free(payload.data);
    }
    logger_log(&logger, LOG_LEVEL_ERROR, "Aborting because root rank failed to prepare payload");
    logger_close(&logger);
    config_free(&config);
    MPI_Finalize();
    return EXIT_FAILURE;
  }

  unsigned long long chunk_size64 = (unsigned long long) config.chunk_size;
  MPI_Bcast(&chunk_size64, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
  config.chunk_size = (size_t) chunk_size64;

  unsigned long long max_req64 = (unsigned long long) config.max_request_bytes;
  MPI_Bcast(&max_req64, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
  config.max_request_bytes = (size_t) max_req64;

  uint64_t payload_len64 = rank == 0 ? (uint64_t) payload.length : 0;
  MPI_Bcast(&payload_len64, 1, MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);
  size_t payload_len = (size_t) payload_len64;
  char *shared_buffer = NULL;
  if (rank == 0) {
    shared_buffer = payload.data;
  } else {
    shared_buffer = malloc(payload_len + 1);
    if (!shared_buffer) {
      logger_log(&logger, LOG_LEVEL_ERROR, "Rank %d cannot allocate %zu bytes for payload", rank, payload_len);
      logger_close(&logger);
      config_free(&config);
      MPI_Finalize();
      return EXIT_FAILURE;
    }
  }

  if (payload_len > 0) {
    if (rank != 0) {
      memset(shared_buffer, 0, payload_len + 1);
    }
    broadcast_payload(shared_buffer, payload_len);
    shared_buffer[payload_len] = '\0';
  }

  Payload shared_payload = {shared_buffer, payload_len};
  bool tui_log_active = false;
  bool original_mirror = logger.mirror_stdout;
  if (config.rank == 0 && config.use_tui) {
    if (tui_log_view_start() == 0) {
      logger_set_sink(&logger, tui_logger_sink, NULL);
      logger.mirror_stdout = false;
      tui_log_active = true;
    } else {
      logger_log(&logger, LOG_LEVEL_WARN, "Unable to initialize TUI log view; falling back to stdout logs");
    }
  }

  process_chunks(&config, &logger, &shared_payload);

  if (tui_log_active) {
    logger_set_sink(&logger, NULL, NULL);
    logger.mirror_stdout = original_mirror;
    tui_log_view_stop();
  }

  if (rank == 0) {
    free(payload.data);
  } else {
    free(shared_buffer);
  }

  logger_log(&logger, LOG_LEVEL_INFO, "Rank %d complete", rank);
  logger_close(&logger);
  config_free(&config);
  MPI_Finalize();
  return EXIT_SUCCESS;
}
