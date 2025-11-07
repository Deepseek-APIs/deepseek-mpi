#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

typedef enum {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR
} LoggerLevel;

typedef struct {
  int process_rank;
  int verbosity;
  bool mirror_stdout;
  void *handle;
} Logger;

int logger_init(Logger *logger, const char *path, int process_rank, int verbosity);
void logger_log(Logger *logger, LoggerLevel level, const char *fmt, ...);
void logger_close(Logger *logger);

#endif /* LOGGER_H */
