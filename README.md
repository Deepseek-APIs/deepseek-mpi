# deepseek-mpi

Distributed TUI client for the DeepSeek Platform API. The utility slices very large text inputs, pushes the chunks to DeepSeek in parallel with MPI ranks, and mirrors results to both the console and a rotating log file. CentOS 7 users can rely on Autotools for predictable builds, while Doxygen keeps the codebase documented. The canonical home is the [`Deepseek-APIs/deepseek-mpi`](https://github.com/Deepseek-APIs/deepseek-mpi) repository, which packages the TUI, CLI, MPI glue, docs, and tests together.

**Live documentation:** https://deepseek-apis.gitbook.io/deepseek-mpi

## Features
- **MPI-driven parallelism** – each rank owns a disjoint set of chunks so API traffic scales horizontally.
- **Ncurses TUI** – interactive session for pasting text or attaching a file by path; can be disabled for batched usage.
- **Configurable CLI** – tune endpoints, chunk sizing, retry windows, logging, stdin/input sources, and more.
- **Robust HTTP client** – libcurl with exponential backoff, deterministic JSON payloads, and OpenAI/Anthropic compatibility.
- **Logging** – mirrored console + file logging with rank annotations for easy traceability.
- **Doxygen docs** – `make doc` renders browsable API docs in `doc/html`.
- **Response archival** – optional `--response-dir` switch persists every chunk response as JSON for audit trails.
- **Deepseek wrapper** – launch `deepseek_wrapper` for a conversational ncurses experience (Codex-like) that automatically spawns MPI jobs.

## Documentation & GitBook

The repository includes a GitBook-ready documentation tree under `/docs` with a `SUMMARY.md` in the repo root. Key pages:

- [docs/README.md](docs/README.md) – landing page for the GitBook space.
- [docs/quickstart.md](docs/quickstart.md) – clean-room builds, dependency installs, running, and testing.
- [docs/cli.md](docs/cli.md) – full CLI flag reference grouped by category.
- [docs/configuration.md](docs/configuration.md) – config files, environment variables, and default values.
- [docs/operations.md](docs/operations.md) – logging, autoscaling runbooks, and wrapper/attachment tips for day‑2 operations.
- [docs/gitbook.md](docs/gitbook.md) – step-by-step instructions for connecting this repo to GitBook, polishing the space, and automating syncs.

Import the repo into GitBook (Organization → New Space → Import from GitHub) and point it at `main`; GitBook will automatically pick up `SUMMARY.md` for the sidebar.

## Requirements
- GCC or Clang (CentOS 7 ships GCC 4.8+)
- OpenMPI or MPICH providing `mpicc`
- libcurl
- ncurses
- readline
- Autotools toolchain (autoconf, automake, libtool)
- Doxygen (optional but recommended for docs)
- Optional: `libmagic`, `libxml2`, and `libarchive` for richer attachment parsing inside the wrapper (CentOS 7 packages: `file-devel`, `libxml2-devel`, `libarchive-devel`). Without them, files still upload via base64.

Install dependencies on CentOS 7 (adjust for your MPI flavor):

```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y openmpi openmpi-devel libcurl-devel ncurses-devel readline-devel autoconf automake libtool doxygen
```

## Bootstrapping & Building
```bash
./autogen.sh
./configure CC=mpicc CFLAGS="-O2 -Wall"
make
```

Run the interactive client with MPI (example uses 4 ranks):

```bash
export DEEPSEEK_API_KEY="sk-live-..."
mpirun -np 4 ./src/deepseek_mpi --log-file=/var/log/deepseek.log --chunk-size=4096
```

Generate API documentation:

```bash
make doc
open doc/html/index.html
```

## CLI Highlights
- `--api-endpoint https://api.deepseek.com/v1/process`
- `--api-provider openai --model gpt-4o-mini` flips the payload/header layout for OpenAI-compatible JSON APIs (also works with Azure, OpenRouter, etc.).
- `--api-provider anthropic --model claude-3-5-sonnet-20240620 --anthropic-version 2023-06-01` targets Anthropic’s official Claude endpoints.
- `--api-provider zai --model glm-4-plus --api-key-env ZAI_API_KEY` talks to z.ai’s GLM APIs (payload schema matches OpenAI chat completions).
- `--api-key-env API_TOKEN`
- `--chunk-size 4096` (bytes per segment)
- `--input-file payload.txt` or `--stdin`
- `--inline-text "quick payload"` bypasses TUI
- `--no-tui` forces batch flow
- `--max-retries 5 --retry-delay-ms 750`
- `--network-retries 2` lets each MPI rank tear down and rebuild its HTTP client after transient network failures before giving up on a chunk
- `--timeout 45` (seconds)
- `--max-request-bytes 12288` guardrail for payload growth
- `--max-output-tokens 1024` clamps completion tokens when using OpenAI/Anthropic providers
- `--dry-run` skips HTTP calls but keeps MPI plumbing and logging
- `--response-dir responses/` streams each chunk response into timestamp-free JSON files per rank
- `--response-files / --no-response-files` toggle per-rank response files (default enabled with directory `responses/`)
- `--wait-exit / --no-wait-exit` pause on completion so you can read the MPI logs before the ranks tear down (default on when attached to a TTY)
- `--readline / --no-readline` choose between GNU Readline prompts or plain stdin when the ncurses TUI is disabled
- `deepseek_wrapper --np 4` opens a chat-style interface and shells out to `mpirun` for every prompt
- `--tasks 16` divides text/CSV/Excel payloads into 16 logical slices so MPI ranks keep working sequentially even if there are more tasks than processes
- `--auto-scale-mode chunks --auto-scale-threshold 100000000 --auto-scale-factor 4` splits giant uploads into additional logical tasks; swap `chunks` for `threads` (via the wrapper) to bump `mpirun -np` automatically once the threshold is crossed
- Provider auto-detect kicks in automatically: if your endpoint contains `openai.com`, `anthropic.com`, or `bigmodel.cn`, your env var is `OPENAI_API_KEY` / `ANTHROPIC_API_KEY` / `ZAI_API_KEY`, or your key begins with well-known prefixes such as `sk-ant-`, `sk-claude`, `gk-`, or `glm-`, the client switches to the matching provider so you don’t have to pass `--api-provider`. Use `--api-provider` to override.

Combine options freely; every flag is also available from a simple key/value config file via `--config my.conf` with entries such as `chunk_size=2048` or `api_endpoint=https://api.deepseek.com/...`.

## Workflow Notes
- The application requires an API key in the environment (`DEEPSEEK_API_KEY` by default).
- `OPENAI_API_KEY` and `ANTHROPIC_API_KEY` are auto-selected when you switch providers (override with `--api-key-env` if your endpoint uses a custom variable name).
- Rank 0 hosts the TUI; non-root ranks wait for the broadcast payload.
- When the TUI is disabled, the client pivots to a GNU Readline prompt (unless `--no-readline`) so you still get history and multiline editing before broadcasting to MPI ranks.
- Logs default to `deepseek_mpi.log` in the working directory; rotate externally if desired.
- Use `--response-dir` when you need deterministic artifacts for downstream pipelines or compliance.
- Response files are enabled by default (saved under `responses/` per rank/chunk). Disable with `--no-response-files` if you only want log output.
- Rank 0 pauses at the end of a run (when attached to a TTY) until you press Enter so you can review the per-rank logs. Disable with `--no-wait-exit` for automated pipelines.
- `--tasks` ensures the entire file (including large spreadsheets) is read once and then auto-sliced, so you’re never limited by the number of hardware threads on the box.
- Autoscaling keeps big drops moving: chunk mode divides payloads across existing ranks, while wrapper `--auto-scale-mode threads` multiplies the MPI rank count on the fly when a prompt crosses your size threshold.
- Provider detection is automatic: endpoints, environment variable names, and well-known key prefixes (Anthropic `sk-ant-`, GLM `gk-`/`glm-`, Azure OpenAI `sk-aoai-`/`sk-az-`, etc.) steer the client toward the right REST API. Explicitly set `--api-provider` if you need to override the heuristic.
- Inside the ncurses TUI, use `:set key=value`, `:show-config`, or `:config-help` to tweak runtime settings (endpoints, chunk sizes, providers, etc.) before launching the MPI job.
- With `--tui` enabled, MPI logs now stream live inside the ncurses window instead of bouncing back to raw stdout, so you can watch rank activity without the screen flicker.
- Use the `deepseek_wrapper` helper when you want something closer to the OpenAI Codex UX; it logs MPI output in-line and reuses the same configuration flags under the hood.
- Use git for change tracking – a clean history keeps regressions easy to spot.
- When you need a guided UX, run `./src/deepseek_wrapper --np 4 --binary ./src/deepseek_mpi`. Every time you hit Enter the wrapper gathers the ongoing conversation, writes a payload file, and invokes `mpirun` with the requested rank count. Type `:quit` or press `Esc` to exit.
- The wrapper understands slash commands: `/help`, `/attach some.png`, `/np 8`, `/tasks 32`, `/chunk 8192`, `/dry-run off`, `/clear`, `/quit`. Attachments automatically detect text vs. binary; documents are inlined and large/binary files are base64 encoded so DeepSeek sees the full content.

### File Attachments & REPL UI
- `/attach` accepts common formats (TXT, CSV, XLSX, PDF, PNG, etc.). Text-based files are streamed directly (with truncation notices for very large files). Binary formats are base64 encoded with metadata.
- The ncurses UI now shows a chat-style history pane, a live REPL prompt, and a "Last Output" panel so you can inspect the raw MPI logs from the previous inference.
- Slash commands let you reconfigure ranks, logical tasks, chunk size, and dry-run mode without restarting the wrapper.

## License
SPDX-License-Identifier: MIT
