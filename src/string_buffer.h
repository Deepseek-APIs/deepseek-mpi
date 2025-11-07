#ifndef STRING_BUFFER_H
#define STRING_BUFFER_H

#include <stdarg.h>
#include <stddef.h>

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} StringBuffer;

void sb_init(StringBuffer *buffer);
int sb_reserve(StringBuffer *buffer, size_t additional);
int sb_append(StringBuffer *buffer, const char *data, size_t len);
int sb_append_str(StringBuffer *buffer, const char *text);
int sb_append_char(StringBuffer *buffer, char ch);
int sb_append_printf(StringBuffer *buffer, const char *fmt, ...);
void sb_reset(StringBuffer *buffer);
char *sb_detach(StringBuffer *buffer);
void sb_clean(StringBuffer *buffer);

#endif /* STRING_BUFFER_H */
