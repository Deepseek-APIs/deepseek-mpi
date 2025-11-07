#include "api_client.h"

#include <curl/curl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
  size_t len = (size_t) needed + 1;
  char *msg = malloc(len);
  if (!msg) {
    va_end(args);
    return;
  }
  vsnprintf(msg, len, fmt, args);
  va_end(args);
  *error_out = msg;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t bytes = size * nmemb;
  if (!userp || bytes == 0) {
    return bytes;
  }
  StringBuffer *buffer = (StringBuffer *) userp;
  if (sb_append(buffer, contents, bytes) != 0) {
    return 0;
  }
  return bytes;
}

static char *json_escape(const char *text, size_t len) {
  if (!text || len == 0) {
    char *empty = malloc(1);
    if (empty) {
      empty[0] = '\0';
    }
    return empty;
  }
  size_t extra = 0;
  for (size_t i = 0; i < len; ++i) {
    unsigned char ch = (unsigned char) text[i];
    switch (ch) {
    case '\\':
    case '\"':
      extra += 1;
      break;
    case '\n':
    case '\r':
    case '\t':
      extra += 1;
      break;
    default:
      if (ch < 0x20) {
        extra += 5;
      }
      break;
    }
  }
  size_t total = len + extra + 1;
  char *escaped = malloc(total);
  if (!escaped) {
    return NULL;
  }
  size_t pos = 0;
  for (size_t i = 0; i < len; ++i) {
    unsigned char ch = (unsigned char) text[i];
    switch (ch) {
    case '\\':
      escaped[pos++] = '\\';
      escaped[pos++] = '\\';
      break;
    case '\"':
      escaped[pos++] = '\\';
      escaped[pos++] = '"';
      break;
    case '\n':
      escaped[pos++] = '\\';
      escaped[pos++] = 'n';
      break;
    case '\r':
      escaped[pos++] = '\\';
      escaped[pos++] = 'r';
      break;
    case '\t':
      escaped[pos++] = '\\';
      escaped[pos++] = 't';
      break;
    default:
      if (ch < 0x20) {
        pos += (size_t) snprintf(escaped + pos, 7, "\\u%04x", ch);
      } else {
        escaped[pos++] = (char) ch;
      }
      break;
    }
  }
  escaped[pos] = '\0';
  return escaped;
}

static char *build_payload(const char *chunk, size_t chunk_len, size_t chunk_index) {
  StringBuffer buffer;
  sb_init(&buffer);
  sb_append_printf(&buffer, "{\"chunk_index\":%zu,\"payload\":\"", chunk_index);
  char *escaped = json_escape(chunk, chunk_len);
  if (!escaped) {
    sb_clean(&buffer);
    return NULL;
  }
  sb_append_str(&buffer, escaped);
  free(escaped);
  sb_append_str(&buffer, "\"}");
  return sb_detach(&buffer);
}

static void sleep_millis(long millis) {
  if (millis <= 0) {
    return;
  }
  struct timespec ts;
  ts.tv_sec = millis / 1000;
  ts.tv_nsec = (millis % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

int api_client_init(ApiClient *client, const ProgramConfig *config, char **error_out) {
  if (!client || !config) {
    assign_error(error_out, "internal: client/config missing");
    return -1;
  }
  memset(client, 0, sizeof *client);
  client->config = config;
  const char *key = config->explicit_api_key;
  if (!key && config->api_key_env) {
    key = getenv(config->api_key_env);
  }
  if (!key && !config->dry_run) {
    assign_error(error_out, "API key expected via %s", config->api_key_env ? config->api_key_env : "environment");
    return -1;
  }
  if (key) {
    client->api_key = strdup(key);
    if (!client->api_key) {
      assign_error(error_out, "Unable to copy API key");
      return -1;
    }
  }
  CURLcode init = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (init != CURLE_OK) {
    assign_error(error_out, "curl init failed: %s", curl_easy_strerror(init));
    free(client->api_key);
    client->api_key = NULL;
    return -1;
  }
  return 0;
}

int api_client_send(ApiClient *client, const char *chunk, size_t chunk_len, size_t chunk_index, StringBuffer *response, char **error_out) {
  if (!client || !client->config) {
    assign_error(error_out, "internal: client missing");
    return -1;
  }
  if (chunk_len > client->config->max_request_bytes) {
    assign_error(error_out, "chunk %zu exceeds max payload %zu", chunk_index, client->config->max_request_bytes);
    return -1;
  }
  if (client->config->dry_run) {
    if (response) {
      sb_reset(response);
      sb_append_printf(response, "{\"chunk\":%zu,\"status\":\"dry-run\"}", chunk_index);
    }
    return 0;
  }

  char *payload = build_payload(chunk, chunk_len, chunk_index);
  if (!payload) {
    assign_error(error_out, "unable to allocate payload");
    return -1;
  }

  int attempts = client->config->max_retries < 0 ? 0 : client->config->max_retries;
  for (int attempt = 0; attempt <= attempts; ++attempt) {
    if (response) {
      sb_reset(response);
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
      assign_error(error_out, "curl handle allocation failed");
      free(payload);
      return -1;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (client->api_key) {
      size_t needed = strlen(client->api_key) + 32;
      char *auth = malloc(needed);
      if (!auth) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(payload);
        assign_error(error_out, "unable to build auth header");
        return -1;
      }
      snprintf(auth, needed, "Authorization: Bearer %s", client->api_key);
      headers = curl_slist_append(headers, auth);
      free(auth);
    }

    curl_easy_setopt(curl, CURLOPT_URL, client->config->api_endpoint);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) strlen(payload));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, client->config->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (client->config->verbosity >= 2) {
      curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc == CURLE_OK && status_code >= 200 && status_code < 300) {
      free(payload);
      return 0;
    }

    if (attempt >= attempts) {
      assign_error(error_out, "HTTP failure rc=%d status=%ld", rc, status_code);
      break;
    }
    sleep_millis(client->config->retry_delay_ms);
  }

  free(payload);
  return -1;
}

void api_client_cleanup(ApiClient *client) {
  if (!client) {
    return;
  }
  free(client->api_key);
  client->api_key = NULL;
  curl_global_cleanup();
}
