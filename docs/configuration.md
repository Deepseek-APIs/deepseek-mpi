---
description: How Deepseek MPI settings interact across defaults, config files, env vars, and CLI.
---

# Configuration Guide

Deepseek MPI exposes every runtime knob through `ProgramConfig` (see `src/app_config.h`). This guide shows how to reason about precedence, defaults, config files, env vars, and reliability settings.

## Precedence Order

Lowest → highest:

1. **Built-in defaults** (compiled in via `config_defaults`).
2. **Config files** loaded with `--config path` (processed in the order provided).
3. **Environment variables**, e.g., `DEEPSEEK_API_KEY` or whatever you set via `--api-key-env`.
4. **CLI flags**, which always override earlier values.

When debugging, run `./src/deepseek_mpi --verbosity 2` to see the resolved configuration in the logs.

## Default Values

| Setting | Default | Notes |
|---------|---------|-------|
| API endpoint | `https://api.deepseek.com/v1/process` | Switching providers rewrites this automatically. |
| API key env var | `DEEPSEEK_API_KEY` | Switches to `OPENAI_API_KEY`/`ANTHROPIC_API_KEY` if you change providers (unless overridden). |
| Chunk size | `2048` bytes | Clamped by `DEEPSEEK_MIN_CHUNK_SIZE`. |
| Max request bytes | `16384` | Guardrail for encoded payload size. |
| Max retries | `3` | libcurl retry attempts per chunk. |
| Retry delay | `500 ms` | Backoff doubles up to ~4 s unless you override. |
| Network retries | `2` | MPI-level client resets after transient network errors. |
| Timeout | `30 s` | `CURLOPT_TIMEOUT`. |
| Progress interval | `1` | Log every chunk per rank unless you increase it. |
| Verbosity | `1` | Level 0 suppresses info logs, level 2 enables debug logs. |
| Max output tokens | `1024` | Used when crafting OpenAI/Anthropic payloads. |
| Show progress | `true` | Toggle with `--hide-progress`. |
| Use TUI | `true` | Disable via `--no-tui`. |
| Dry run | `false` | No HTTP requests when enabled. |

## Config Files

Config files are plain `key=value` documents processed before CLI flags. Supported keys include:

- Endpoint & auth: `api_endpoint`, `api_key_env`, `api_key`, `api_provider`, `model`, `anthropic_version`.
- Chunking & limits: `chunk_size`, `max_request_bytes`, `tasks`.
- Reliability: `max_retries`, `network_retries`, `retry_delay_ms`, `timeout`.
- Logging & UX: `log_file`, `response_dir`, `progress_interval`, `verbosity`, `show_progress`, `use_tui`, `dry_run`, `force_quiet`.
- Inputs: `input_file`, `inline_text`, `use_stdin`, `allow_file_prompt`.

Example (`config/production.conf`):

```ini
api_provider=openai
api_endpoint=https://api.openai.com/v1/chat/completions
api_key_env=OPENAI_API_KEY
model=gpt-4o-mini
chunk_size=4096
max_request_bytes=65536
max_retries=5
network_retries=4
retry_delay_ms=750
timeout=45
response_dir=/var/log/deepseek/responses
tasks=16
show_progress=true
```

Load multiple files if needed:

```bash
./src/deepseek_mpi --config config/base.conf --config config/prod-overrides.conf …
```

## Environment Variables

- **API keys:** whichever variable `--api-key-env` names (`DEEPSEEK_API_KEY`, `OPENAI_API_KEY`, etc.).
- **MPI stack:** anything your MPI implementation expects (`OMPI_MCA_*`, `PMI_*`) continues to work; Deepseek MPI doesn’t override them.
- **Logging:** set `MPICH_ASYNC_PROGRESS=1`, etc., if your runtime benefits from it—those are orthogonal to our config.

## Reliability Knobs

- `max_retries` + `retry_delay_ms`: control libcurl-level retries. When requests fail with HTTP 429/5xx or network issues, `api_client` delays and retries up to this count.
- `network_retries`: when curl reports persistent network failures, the MPI rank will tear down and rebuild the entire HTTP client this many times before giving up on the chunk.
- `timeout`: ensures a hung request doesn’t block an MPI rank indefinitely.

Track the aggregated stats in the logs—rank 0 prints `processed`, `failures`, and `network_failures` once all ranks finish.

## Response Persistence

Set `response_dir` to a writable path to capture every successful chunk response as `chunk-<index>-r<rank>.json`. This is useful for audit trails, debugging, and offline processing.

## Attachment & Wrapper Dependencies

If you build with `libxml2`, `libmagic`, and `libarchive`, the `deepseek_wrapper` binary can sniff and ingest more attachment types. The MPI core is happy without them; missing libraries simply disable those extras.

## Related Pages

- [Quickstart](quickstart.md) – install, build, run, and test instructions.
- [CLI Reference](cli.md) – every flag grouped by category.
- [GitBook Publishing](gitbook.md) – how to publicize these docs.
