---
description: Every flag exposed by deepseek_mpi, grouped by category.
---

# CLI Reference

Deepseek MPI exposes a comprehensive CLI so you can control inputs, API providers, chunk sizing, retries, and logging from the command line or scripts. Every flag has a long-form equivalent; short forms are shown where applicable.

## Input & Payload Capture

| Flag | Description |
|------|-------------|
| `--input-file PATH`, `-f PATH` | Read payload from file (`-` for stdin). |
| `--stdin`, `-S` | Force stdin even without `--input-file -`. |
| `--inline-text STRING`, `-T STRING` | Provide payload inline (disables TUI). |
| `--tui` / `--no-tui` | Enable or disable the ncurses prompt on rank 0. |
| `--tasks N` | Desired logical task count; chunk size auto-adjusts. |
| `--allow-file-prompt` | Enable TUI file attach (default on). |

## API Provider & Authentication

| Flag | Description |
|------|-------------|
| `--api-endpoint URL`, `-e URL` | Override the HTTP endpoint. Defaults auto-switch when you change providers. |
| `--api-provider NAME` | `deepseek` (default), `openai`, or `anthropic`. |
| `--model ID`, `-m ID` | Model identifier for OpenAI/Anthropic providers (e.g., `gpt-4o-mini`, `claude-3-5-sonnet-20240620`). |
| `--anthropic-version DATE` | Override the `anthropic-version` header (defaults to `2023-06-01`). |
| `--api-key-env NAME`, `-k NAME` | Environment variable to read the API key from. |
| `--api-key VALUE` | Inline API key (overrides env). Handy for CI secrets or single-use keys. |

## Chunking & Payload Limits

| Flag | Description |
|------|-------------|
| `--chunk-size BYTES`, `-c BYTES` | Fixed chunk size per logical task. Minimum enforced via `DEEPSEEK_MIN_CHUNK_SIZE`. |
| `--max-request-bytes BYTES` | Upper bound for encoded payload (defaults to ≥ chunk size). |
| `--max-output-tokens N` | Clamp model responses for OpenAI/Anthropic backends. |

## Reliability & Retries

| Flag | Description |
|------|-------------|
| `--max-retries N`, `-r N` | Per-chunk HTTP retries before failing. |
| `--retry-delay-ms MS`, `-d MS` | Base delay between HTTP retries (libcurl handler doubles it until capped). |
| `--network-retries N` | Number of times an MPI rank can tear down and rebuild its HTTP client after network failures (default `2`). |
| `--timeout SECONDS`, `-t SECONDS` | HTTP timeout per request. |

## Logging & Telemetry

| Flag | Description |
|------|-------------|
| `--log-file PATH`, `-l PATH` | Append logs for each rank (stdout mirroring stays on by default). |
| `--response-dir DIR` | Persist each successful chunk response to JSON files. |
| `--verbose`, `-v` | Increase verbosity (debug logging at level 2). |
| `--quiet`, `-q` | Disable log mirroring (forces verbosity 0). |
| `--progress-interval N`, `-p N` | Print a progress log entry every N chunks per rank. |

## Execution Flow

| Flag | Description |
|------|-------------|
| `--dry-run` | Skip HTTP calls; still slices payloads and exercises MPI/logging. |
| `--config FILE` | Load `key=value` defaults before processing CLI flags. |
| `--upload PATH` | Legacy alias for `--input-file`. |
| `--version` | Print build version and exit. |
| `--help`, `-h` | CLI usage summary. |

## Examples

```bash
# OpenAI provider with explicit model, Anthropic-style config, and response mirroring
mpirun -np 4 ./src/deepseek_mpi \
  --api-provider openai \
  --model gpt-4o-mini \
  --api-key-env OPENAI_API_KEY \
  --tasks 32 \
  --response-dir responses/

# Batch job from stdin with aggressive resiliency
cat payload.txt | mpirun -np 8 ./src/deepseek_mpi \
  --stdin \
  --chunk-size 4096 \
  --max-request-bytes 16384 \
  --max-retries 5 \
  --retry-delay-ms 750 \
  --network-retries 3
```

Refer back to the [Configuration guide](configuration.md) for default values, environment variables, and config-file syntax. GitBook users can expose these tables as separate chapters if desired; simply adjust `SUMMARY.md` accordingly.
