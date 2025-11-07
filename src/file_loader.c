#include "file_loader.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_LOADER_CHUNK 4096

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

static int read_stream_internal(FILE *stream, char **out, size_t *len, char **error_out) {
  if (!stream || !out || !len) {
    return -1;
  }
  char *buffer = NULL;
  size_t capacity = 0;
  size_t used = 0;
  char chunk[FILE_LOADER_CHUNK];
  while (1) {
    size_t read_bytes = fread(chunk, 1, sizeof chunk, stream);
    if (read_bytes > 0) {
      size_t required = used + read_bytes + 1;
      if (required > capacity) {
        size_t new_capacity = capacity ? capacity : required;
        while (new_capacity < required) {
          new_capacity *= 2;
        }
        char *next = realloc(buffer, new_capacity);
        if (!next) {
          free(buffer);
          assign_error(error_out, "Out of memory while reading stream");
          return -1;
        }
        buffer = next;
        capacity = new_capacity;
      }
      memcpy(buffer + used, chunk, read_bytes);
      used += read_bytes;
    }
    if (read_bytes < sizeof chunk) {
      if (ferror(stream)) {
        free(buffer);
        assign_error(error_out, "Error while reading stream: %s", strerror(errno));
        return -1;
      }
      break;
    }
  }
  if (!buffer) {
    buffer = malloc(1);
    if (!buffer) {
      assign_error(error_out, "Unable to allocate buffer");
      return -1;
    }
  }
  buffer[used] = '\0';
  *out = buffer;
  *len = used;
  return 0;
}

int file_loader_read_stream(FILE *stream, char **out, size_t *len, char **error_out) {
  return read_stream_internal(stream, out, len, error_out);
}

int file_loader_read_all(const char *path, char **out, size_t *len, char **error_out) {
  if (!path) {
    assign_error(error_out, "input path missing");
    return -1;
  }
  if (strcmp(path, "-") == 0) {
    return file_loader_read_stream(stdin, out, len, error_out);
  }
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    assign_error(error_out, "Unable to open %s: %s", path, strerror(errno));
    return -1;
  }
  int rc = read_stream_internal(fp, out, len, error_out);
  fclose(fp);
  return rc;
}
