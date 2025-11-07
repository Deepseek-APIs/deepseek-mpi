#ifndef ATTACHMENT_LOADER_H
#define ATTACHMENT_LOADER_H

#include <stddef.h>

typedef struct {
  char *message_text;
  char *mime_label;
  int is_textual;
} AttachmentResult;

int attachment_format_message(const char *path, AttachmentResult *result, char **error_out);
void attachment_result_clean(AttachmentResult *result);

#endif /* ATTACHMENT_LOADER_H */
