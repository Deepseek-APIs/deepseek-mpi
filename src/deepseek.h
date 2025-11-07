#ifndef DEEPSEEK_H
#define DEEPSEEK_H

/**
 * @file
 * @brief Global constants shared across the DeepSeek MPI client.
 */

#define DEEPSEEK_DEFAULT_ENDPOINT        "https://api.deepseek.com/v1/process"
#define DEEPSEEK_DEFAULT_CHUNK_SIZE      2048ULL
#define DEEPSEEK_MIN_CHUNK_SIZE          128ULL
#define DEEPSEEK_DEFAULT_MAX_REQUEST     16384ULL
#define DEEPSEEK_DEFAULT_TIMEOUT_SECONDS 30L
#define DEEPSEEK_DEFAULT_RETRIES         3
#define DEEPSEEK_DEFAULT_RETRY_DELAY_MS  500L
#define DEEPSEEK_DEFAULT_LOG_FILE        "deepseek_mpi.log"

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
