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

static char *build_payload_deepseek(const char *chunk, size_t chunk_len, size_t chunk_index) {
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

static const char *resolve_model(const ProgramConfig *config, ApiProvider provider) {
  if (config && config->model && config->model[0] != '\0') {
    return config->model;
  }
  switch (provider) {
  case API_PROVIDER_OPENAI:
    return OPENAI_DEFAULT_MODEL;
  case API_PROVIDER_ANTHROPIC:
    return ANTHROPIC_DEFAULT_MODEL;
  case API_PROVIDER_ZAI:
    return ZAI_DEFAULT_MODEL;
  default:
    return "";
  }
}

static int resolve_max_tokens(const ProgramConfig *config) {
  if (!config) {
    return AI_DEFAULT_MAX_OUTPUT_TOKENS;
  }
  if (config->max_output_tokens > 0) {
    return config->max_output_tokens;
  }
  return AI_DEFAULT_MAX_OUTPUT_TOKENS;
}

static char *build_payload_openai_style(const ProgramConfig *config, const char *chunk, size_t chunk_len,
                                        ApiProvider provider) {
  const char *model = resolve_model(config, provider);
  int max_tokens = resolve_max_tokens(config);
  StringBuffer buffer;
  sb_init(&buffer);
  sb_append_str(&buffer, "{\"model\":\"");
  sb_append_str(&buffer, model);
  sb_append_str(&buffer, "\",\"messages\":[{\"role\":\"user\",\"content\":\"");
  char *escaped = json_escape(chunk, chunk_len);
  if (!escaped) {
    sb_clean(&buffer);
    return NULL;
  }
  sb_append_str(&buffer, escaped);
  free(escaped);
  sb_append_str(&buffer, "\"}]");
  if (max_tokens > 0) {
    sb_append_printf(&buffer, ",\"max_tokens\":%d", max_tokens);
  }
  sb_append_char(&buffer, '}');
  return sb_detach(&buffer);
}

static char *build_payload_anthropic(const ProgramConfig *config, const char *chunk, size_t chunk_len) {
  const char *model = resolve_model(config, API_PROVIDER_ANTHROPIC);
  int max_tokens = resolve_max_tokens(config);
  StringBuffer buffer;
  sb_init(&buffer);
  sb_append_str(&buffer, "{\"model\":\"");
  sb_append_str(&buffer, model);
  sb_append_printf(&buffer, "\",\"max_tokens\":%d,\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"",
                   max_tokens);
  char *escaped = json_escape(chunk, chunk_len);
  if (!escaped) {
    sb_clean(&buffer);
    return NULL;
  }
  sb_append_str(&buffer, escaped);
  free(escaped);
  sb_append_str(&buffer, "\"}]}]}");
  return sb_detach(&buffer);
}

static char *build_payload_for_provider(const ProgramConfig *config, const char *chunk, size_t chunk_len,
                                        size_t chunk_index) {
  if (!config) {
    return NULL;
  }
  switch (config->provider) {
  case API_PROVIDER_OPENAI:
    return build_payload_openai_style(config, chunk, chunk_len, API_PROVIDER_OPENAI);
  case API_PROVIDER_ANTHROPIC:
    return build_payload_anthropic(config, chunk, chunk_len);
  case API_PROVIDER_ZAI:
    return build_payload_openai_style(config, chunk, chunk_len, API_PROVIDER_ZAI);
  case API_PROVIDER_DEEPSEEK:
  default:
    return build_payload_deepseek(chunk, chunk_len, chunk_index);
  }
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

int api_client_send(ApiClient *client, const char *chunk, size_t chunk_len, size_t chunk_index, StringBuffer *response,
                    char **error_out, ApiClientError *error_type) {
  if (error_type) {
    *error_type = API_CLIENT_ERROR_NONE;
  }
  if (!client || !client->config) {
    assign_error(error_out, "internal: client missing");
    if (error_type) {
      *error_type = API_CLIENT_ERROR_PERMANENT;
    }
    return -1;
  }
  if (chunk_len > client->config->max_request_bytes) {
    assign_error(error_out, "chunk %zu exceeds max payload %zu", chunk_index, client->config->max_request_bytes);
    if (error_type) {
      *error_type = API_CLIENT_ERROR_PERMANENT;
    }
    return -1;
  }
  if (client->config->dry_run) {
    if (response) {
      sb_reset(response);
      sb_append_printf(response, "{\"chunk\":%zu,\"status\":\"dry-run\"}", chunk_index);
    }
    return 0;
  }

  char *payload = build_payload_for_provider(client->config, chunk, chunk_len, chunk_index);
  if (!payload) {
    assign_error(error_out, "unable to allocate payload");
    if (error_type) {
      *error_type = API_CLIENT_ERROR_PERMANENT;
    }
    return -1;
  }

  int attempts = client->config->max_retries < 0 ? 0 : client->config->max_retries;
  long base_delay = client->config->retry_delay_ms > 0 ? client->config->retry_delay_ms : 100;
  if (base_delay <= 0) {
    base_delay = 100;
  }
  long delay = base_delay;
  long max_delay = base_delay * 8;
  if (max_delay < base_delay) {
    max_delay = base_delay;
  }

  ApiClientError final_error = API_CLIENT_ERROR_HTTP;

  for (int attempt = 0; attempt <= attempts; ++attempt) {
    if (response) {
      sb_reset(response);
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
      assign_error(error_out, "curl handle allocation failed");
      free(payload);
      if (error_type) {
        *error_type = API_CLIENT_ERROR_PERMANENT;
      }
      return -1;
    }
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (client->config->provider == API_PROVIDER_ANTHROPIC) {
      if (!client->api_key) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(payload);
        assign_error(error_out, "Anthropic-compatible endpoints require an API key");
        if (error_type) {
          *error_type = API_CLIENT_ERROR_PERMANENT;
        }
        return -1;
      }
      size_t key_needed = strlen(client->api_key) + 16;
      char *key_header = malloc(key_needed);
      if (!key_header) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(payload);
        assign_error(error_out, "unable to allocate x-api-key header");
        if (error_type) {
          *error_type = API_CLIENT_ERROR_PERMANENT;
        }
        return -1;
      }
      snprintf(key_header, key_needed, "x-api-key: %s", client->api_key);
      headers = curl_slist_append(headers, key_header);
      free(key_header);

      const char *version =
          client->config->anthropic_version ? client->config->anthropic_version : ANTHROPIC_DEFAULT_VERSION;
      size_t version_len = strlen(version) + 32;
      char *version_header = malloc(version_len);
      if (!version_header) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(payload);
        assign_error(error_out, "unable to allocate anthropic-version header");
        if (error_type) {
          *error_type = API_CLIENT_ERROR_PERMANENT;
        }
        return -1;
      }
      snprintf(version_header, version_len, "anthropic-version: %s", version);
      headers = curl_slist_append(headers, version_header);
      free(version_header);
    } else if (client->api_key) {
      size_t needed = strlen(client->api_key) + 32;
      char *auth = malloc(needed);
      if (!auth) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(payload);
        assign_error(error_out, "unable to build auth header");
        if (error_type) {
          *error_type = API_CLIENT_ERROR_PERMANENT;
        }
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

    bool network_error = (rc != CURLE_OK);
    bool http_transient =
        (rc == CURLE_OK && (status_code == 0 || status_code == 408 || status_code == 429 || status_code >= 500));
    bool transient = network_error || http_transient;
    final_error = transient ? API_CLIENT_ERROR_NETWORK : API_CLIENT_ERROR_HTTP;

    if (attempt >= attempts || !transient) {
      if (network_error) {
        assign_error(error_out, "network failure rc=%d (%s)", rc, curl_easy_strerror(rc));
      } else {
        assign_error(error_out, "HTTP failure status=%ld", status_code);
      }
      break;
    }

    sleep_millis(delay);
    if (delay < max_delay) {
      long next = delay * 2;
      delay = next > max_delay ? max_delay : next;
    }
  }

  if (error_type) {
    *error_type = final_error;
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
