#ifndef DEEPSEEK_H
#define DEEPSEEK_H

/**
 * @file
 * @brief Global constants shared across the DeepSeek MPI client.
 */

#define DEEPSEEK_DEFAULT_ENDPOINT        "https://api.deepseek.com/chat/completions"
#define DEEPSEEK_DEFAULT_CHUNK_SIZE      2048ULL
#define DEEPSEEK_MIN_CHUNK_SIZE          128ULL
#define DEEPSEEK_DEFAULT_MAX_REQUEST     16384ULL
#define DEEPSEEK_DEFAULT_TIMEOUT_SECONDS 30L
#define DEEPSEEK_DEFAULT_RETRIES         3
#define DEEPSEEK_DEFAULT_RETRY_DELAY_MS  500L
#define DEEPSEEK_DEFAULT_NETWORK_RESETS  2
#define DEEPSEEK_DEFAULT_LOG_FILE        "deepseek_mpi.log"
#define DEEPSEEK_DEFAULT_RESPONSE_DIR    "responses"
#define DEEPSEEK_DEFAULT_API_ENV         "DEEPSEEK_API_KEY"
#define DEEPSEEK_DEFAULT_MODEL           "deepseek-chat"

#define OPENAI_DEFAULT_ENDPOINT          "https://api.openai.com/v1/chat/completions"
#define OPENAI_DEFAULT_MODEL             "gpt-4o-mini"
#define OPENAI_DEFAULT_API_ENV           "OPENAI_API_KEY"

#define ZAI_DEFAULT_ENDPOINT             "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#define ZAI_DEFAULT_MODEL                "glm-4-plus"
#define ZAI_DEFAULT_API_ENV              "ZAI_API_KEY"

#define ANTHROPIC_DEFAULT_ENDPOINT       "https://api.anthropic.com/v1/messages"
#define ANTHROPIC_DEFAULT_MODEL          "claude-3-5-sonnet-20240620"
#define ANTHROPIC_DEFAULT_API_ENV        "ANTHROPIC_API_KEY"
#define ANTHROPIC_DEFAULT_VERSION        "2023-06-01"

#define AI_DEFAULT_MAX_OUTPUT_TOKENS     1024
#define DEEPSEEK_AUTOSCALE_DEFAULT_THRESHOLD (100ULL * 1024ULL * 1024ULL)
#define DEEPSEEK_AUTOSCALE_DEFAULT_FACTOR    2

/**
 * @return build-time version string advertised to users.
 */
static inline const char *deepseek_get_version(void) {
#ifdef PACKAGE_VERSION
  return PACKAGE_VERSION;
#else
  return "dev";
#endif
}

#endif /* DEEPSEEK_H */
