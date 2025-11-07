#include "string_buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sb_init(StringBuffer *buffer) {
  if (!buffer) {
    return;
  }
  buffer->data = malloc(1);
  if (buffer->data) {
    buffer->data[0] = '\0';
  }
  buffer->length = 0;
  buffer->capacity = buffer->data ? 1 : 0;
}

static int sb_grow(StringBuffer *buffer, size_t needed) {
  if (!buffer) {
    return -1;
  }
  size_t required = buffer->length + needed + 1;
  if (required <= buffer->capacity) {
    return 0;
  }
  size_t new_cap = buffer->capacity ? buffer->capacity : 1;
  while (new_cap < required) {
    new_cap *= 2;
  }
  char *next = realloc(buffer->data, new_cap);
  if (!next) {
    return -1;
  }
  buffer->data = next;
  buffer->capacity = new_cap;
  return 0;
}

int sb_reserve(StringBuffer *buffer, size_t additional) {
  return sb_grow(buffer, additional);
}

int sb_append(StringBuffer *buffer, const char *data, size_t len) {
  if (!buffer || !data) {
    return -1;
  }
  if (sb_grow(buffer, len) != 0) {
    return -1;
  }
  memcpy(buffer->data + buffer->length, data, len);
  buffer->length += len;
  buffer->data[buffer->length] = '\0';
  return 0;
}

int sb_append_str(StringBuffer *buffer, const char *text) {
  return text ? sb_append(buffer, text, strlen(text)) : 0;
}

int sb_append_char(StringBuffer *buffer, char ch) {
  return sb_append(buffer, &ch, 1);
}

int sb_append_printf(StringBuffer *buffer, const char *fmt, ...) {
  if (!buffer || !fmt) {
    return -1;
  }
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, fmt, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return -1;
  }
  if (sb_grow(buffer, (size_t) needed) != 0) {
    va_end(args);
    return -1;
  }
  vsnprintf(buffer->data + buffer->length, (size_t) needed + 1, fmt, args);
  buffer->length += (size_t) needed;
  va_end(args);
  return 0;
}

void sb_reset(StringBuffer *buffer) {
  if (!buffer) {
    return;
  }
  if (!buffer->data && buffer->capacity == 0) {
    sb_init(buffer);
    return;
  }
  if (buffer->capacity > 0 && buffer->data) {
    buffer->data[0] = '\0';
  }
  buffer->length = 0;
}

char *sb_detach(StringBuffer *buffer) {
  if (!buffer) {
    return NULL;
  }
  char *result = buffer->data;
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
  return result;
}

void sb_clean(StringBuffer *buffer) {
  if (!buffer) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}
