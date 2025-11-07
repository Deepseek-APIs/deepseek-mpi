#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <stddef.h>

#include "app_config.h"
#include "string_buffer.h"

typedef struct {
  const ProgramConfig *config;
  char *api_key;
} ApiClient;

int api_client_init(ApiClient *client, const ProgramConfig *config, char **error_out);
int api_client_send(ApiClient *client, const char *chunk, size_t chunk_len, size_t chunk_index, StringBuffer *response, char **error_out);
void api_client_cleanup(ApiClient *client);

#endif /* API_CLIENT_H */
