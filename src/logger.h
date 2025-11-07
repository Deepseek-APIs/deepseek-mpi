#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

typedef enum {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR
} LoggerLevel;

typedef struct Logger Logger;

typedef void (*LoggerSinkFn)(LoggerLevel level, int process_rank, const char *timestamp, const char *message, void *user_data);

struct Logger {
  int process_rank;
  int verbosity;
  bool mirror_stdout;
  void *handle;
  LoggerSinkFn sink;
  void *sink_user_data;
};

int logger_init(Logger *logger, const char *path, int process_rank, int verbosity);
void logger_log(Logger *logger, LoggerLevel level, const char *fmt, ...);
void logger_close(Logger *logger);
void logger_set_sink(Logger *logger, LoggerSinkFn sink, void *user_data);
const char *logger_level_to_string(LoggerLevel level);

#endif /* LOGGER_H */
