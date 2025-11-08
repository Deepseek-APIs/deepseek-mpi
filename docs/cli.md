---
description: Every flag exposed by deepseek_mpi, grouped by category.
---

# CLI Reference

Deepseek MPI exposes a comprehensive CLI so you can control inputs, API providers, chunk sizing, retries, autoscaling, and logging from the command line or scripts. Every flag has a long-form equivalent; short forms are shown where applicable.

## Input & Payload Capture

| Flag | Description |
|------|-------------|
| `--input-file PATH`, `-f PATH` | Read payload from file (`-` for stdin). |
| `--stdin`, `-S` | Force stdin even without `--input-file -`. |
| `--inline-text STRING`, `-T STRING` | Provide payload inline (disables TUI). |
| `--tui` / `--no-tui` | Enable or disable the ncurses prompt on rank 0. |
| `--tasks N` | Desired logical task count; chunk size auto-adjusts. |
| `--allow-file-prompt` | Enable TUI file attach (default on). |
| `--readline` / `--no-readline` | Toggle the GNU Readline prompt used when the TUI is disabled. |
| `--repl` | Keep `deepseek_mpi` running in an interactive REPL; previous prompts/responses are threaded into the next prompt. |

In the standard ncurses TUI and the Readline prompt, finish your payload by typing a single `.` on its own line. When `--repl` is enabled, the split-window UI adds an upload field above the prompt: use `Tab` to toggle between them, Enter on the upload field to pull a file into the buffer, and `Ctrl+K` to send the accumulated prompt.

## API Provider & Authentication

| Flag | Description |
|------|-------------|
| `--api-endpoint URL`, `-e URL` | Override the HTTP endpoint. Defaults auto-switch when you change providers. |
| `--api-provider NAME` | `deepseek` (default), `openai`, `anthropic`, or `zai` (GLM from z.ai). |
| `--model ID`, `-m ID` | Model identifier for OpenAI/Anthropic/Zai providers (e.g., `gpt-4o-mini`, `claude-3-5-sonnet-20240620`, `glm-4-plus`). |
| `--anthropic-version DATE` | Override the `anthropic-version` header (defaults to `2023-06-01`). |
| `--api-key-env NAME`, `-k NAME` | Environment variable to read the API key from. |
| `--api-key VALUE` | Inline API key (overrides env). Handy for CI secrets or single-use keys. |

## Chunking & Payload Limits

| Flag | Description |
|------|-------------|
| `--chunk-size BYTES`, `-c BYTES` | Fixed chunk size per logical task. Minimum enforced via `DEEPSEEK_MIN_CHUNK_SIZE`. |
| `--max-request-bytes BYTES` | Upper bound for encoded payload (defaults to ≥ chunk size). |
| `--max-output-tokens N` | Clamp model responses for OpenAI/Anthropic backends. |
| `--auto-scale-mode MODE` | `none`, `chunks`, or `threads`. Chunks mode multiplies `--tasks`; threads mode logs guidance (wrapper handles actual rank scaling). |
| `--auto-scale-threshold BYTES` | Trigger size for autoscaling. |
| `--auto-scale-factor N` | Multiplier applied when the threshold is exceeded. |

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
| `--tui-log-view` / `--no-tui-log-view` | Controls the post-prompt ncurses pane that streams MPI logs (auto-enabled when `--tui`; auto mode hides chunk/progress noise, passing `--tui-log-view` explicitly keeps the full stream). |

## Execution Flow

| Flag | Description |
|------|-------------|
| `--dry-run` | Skip HTTP calls; still slices payloads and exercises MPI/logging. |
| `--config FILE` | Load `key=value` defaults before processing CLI flags. |
| `--upload PATH` | Legacy alias for `--input-file`. |
| `--version` | Print build version and exit. |
| `--help`, `-h` | CLI usage summary. |
| `.` (single dot line) | When using the default TUI or `--no-tui --readline`, enter a lone `.` on a new line to send the buffered payload (use `Ctrl+K` inside the REPL TUI). |

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

# Autoscale example (chunks mode)
mpirun -np 4 ./src/deepseek_mpi \
  --input-file huge.md \
  --auto-scale-mode chunks \
  --auto-scale-threshold 150000000 \
  --auto-scale-factor 4
```

Refer back to the [Configuration guide](configuration.md) for default values, environment variables, and config-file syntax. GitBook users can expose these tables as separate chapters if desired; simply adjust `SUMMARY.md` accordingly.

## Wrapper Flags (Preview)

The `deepseek_wrapper` binary has its own CLI; highlights:

| Flag | Description |
| --- | --- |
| `--np N` | MPI ranks per prompt (default `2`). |
| `--binary PATH` | Path to `deepseek_mpi` (default `./src/deepseek_mpi`). |
| `--response-dir DIR` | Where to store per-chunk responses (default `responses/`). |
| `--log-file PATH` | Override the `deepseek_mpi` log file (alias `--log`). |
| `--verbose` | Increase console verbosity for the wrapped `deepseek_mpi` process (repeatable). |
| `--auto-scale-mode MODE` | Same semantics as the core autoscale mode, but threads mode actually bumps `mpirun -np`. |
| `--auto-scale-threshold BYTES` | Payload size that triggers autoscaling. |
| `--auto-scale-factor N` | Multiplier for tasks/ranks. |
| `--auto-scale-max-np N` | Cap on autoscaled ranks (prevents runaway concurrency). |

Wrapper slash commands mirror these flags, so operators can tweak runs without leaving the terminal.
