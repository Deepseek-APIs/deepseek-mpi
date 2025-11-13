#ifndef ATTACHMENT_LOADER_H
#define ATTACHMENT_LOADER_H

#include <stddef.h>

#include <stdbool.h>

typedef struct {
  char *message_text;
  char *mime_label;
  int is_textual;
} AttachmentResult;

typedef struct {
  char *data;
  size_t length;
  char *mime_label;
  bool extracted_from_container;
  bool is_textual;
  bool encoded_binary;
} AttachmentTextPayload;

int attachment_format_message(const char *path, AttachmentResult *result, char **error_out);
void attachment_result_clean(AttachmentResult *result);
int attachment_extract_text_payload(const char *path, AttachmentTextPayload *payload, char **error_out);
void attachment_text_payload_clean(AttachmentTextPayload *payload);

#endif /* ATTACHMENT_LOADER_H */
