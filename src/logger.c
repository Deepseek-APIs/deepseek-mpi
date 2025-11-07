#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *level_to_string(LoggerLevel level) {
  switch (level) {
  case LOG_LEVEL_DEBUG:
    return "DEBUG";
  case LOG_LEVEL_INFO:
    return "INFO";
  case LOG_LEVEL_WARN:
    return "WARN";
  case LOG_LEVEL_ERROR:
  default:
    return "ERROR";
  }
}

static int level_allowed(const Logger *logger, LoggerLevel level) {
  if (!logger) {
    return 0;
  }
  if (level == LOG_LEVEL_DEBUG) {
    return logger->verbosity >= 2;
  }
  if (level == LOG_LEVEL_INFO) {
    return logger->verbosity >= 1;
  }
  return 1;
}

int logger_init(Logger *logger, const char *path, int process_rank, int verbosity) {
  if (!logger) {
    return -1;
  }
  logger->process_rank = process_rank;
  logger->verbosity = verbosity;
  logger->mirror_stdout = true;
  logger->handle = NULL;
  if (path) {
    FILE *fp = fopen(path, "a");
    if (!fp) {
      return -1;
    }
    setvbuf(fp, NULL, _IOLBF, 0);
    logger->handle = fp;
  }
  return 0;
}

void logger_log(Logger *logger, LoggerLevel level, const char *fmt, ...) {
  if (!logger) {
    return;
  }
  if (!level_allowed(logger, level)) {
    return;
  }
  FILE *fp = (FILE *) logger->handle;
  time_t now = time(NULL);
  struct tm tm_now;
  localtime_r(&now, &tm_now);
  char timestamp[32];
  strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &tm_now);

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
  char *line = malloc(size);
  if (!line) {
    va_end(args);
    return;
  }
  vsnprintf(line, size, fmt, args);
  va_end(args);

  if (logger->mirror_stdout) {
    fprintf(stdout, "[%s] %s [rank %d] | %s\n", timestamp, level_to_string(level), logger->process_rank, line);
    fflush(stdout);
  }
  if (fp) {
    fprintf(fp, "[%s] %s [rank %d] | %s\n", timestamp, level_to_string(level), logger->process_rank, line);
    fflush(fp);
  }
  free(line);
}

void logger_close(Logger *logger) {
  if (!logger) {
    return;
  }
  FILE *fp = (FILE *) logger->handle;
  if (fp) {
    fclose(fp);
  }
  logger->handle = NULL;
}
