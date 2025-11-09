# deepseek-mpi

Distributed TUI client for the DeepSeek Platform API. The utility slices very large text inputs, pushes the chunks to DeepSeek in parallel with MPI ranks, and mirrors results to both the console and a rotating log file. CentOS 7 users can rely on Autotools for predictable builds, while Doxygen keeps the codebase documented. The canonical home is the [`Deepseek-APIs/deepseek-mpi`](https://github.com/Deepseek-APIs/deepseek-mpi) repository, which packages the TUI, CLI, MPI glue, docs, and tests together.

**Live documentation:** https://deepseek-apis.gitbook.io/deepseek-mpi

## Features
- **MPI-driven parallelism** – each rank owns a disjoint set of chunks so API traffic scales horizontally.
- **Ncurses TUI** – interactive session for authoring prompts on rank 0 (finish with `.`); fall back to GNU Readline or stdin for batch jobs.
- **Integrated REPL UI** – `--repl` turns the ncurses prompt into a chat-style experience with scrollback, file staging, and status panes without needing a separate wrapper binary.
- **Configurable CLI** – tune endpoints, chunk sizing, retry windows, logging, stdin/input sources, and more.
- **Robust HTTP client** – libcurl with exponential backoff, deterministic JSON payloads, and OpenAI/Anthropic compatibility.
- **Logging** – mirrored console + file logging with rank annotations for easy traceability.
- **Doxygen docs** – `make doc` renders browsable API docs in `doc/html`.
- **Response archival** – optional `--response-dir` switch persists every chunk response as JSON for audit trails.

## Documentation & GitBook

The repository includes a GitBook-ready documentation tree under `/docs` with a `SUMMARY.md` in the repo root. Key pages:

- [docs/README.md](docs/README.md) – landing page for the GitBook space.
- [docs/quickstart.md](docs/quickstart.md) – clean-room builds, dependency installs, running, and testing.
- [docs/cli.md](docs/cli.md) – full CLI flag reference grouped by category.
- [docs/configuration.md](docs/configuration.md) – config files, environment variables, and default values.
- [docs/operations.md](docs/operations.md) – logging, autoscaling runbooks, and REPL/TUI tips for day‑2 operations.
- [docs/gitbook.md](docs/gitbook.md) – step-by-step instructions for connecting this repo to GitBook, polishing the space, and automating syncs.

Import the repo into GitBook (Organization → New Space → Import from GitHub) and point it at `main`; GitBook will automatically pick up `SUMMARY.md` for the sidebar.

## Requirements
- GCC or Clang (CentOS 7 ships GCC 4.8+)
- OpenMPI or MPICH providing `mpicc`
- libcurl
- ncurses
- readline
- Autotools toolchain (autoconf, automake, libtool)
- Doxygen (optional but recommended for docs)
- Optional: `libmagic`, `libxml2`, and `libarchive`. `configure` probes for them (leftover from the legacy wrapper) but the current build works fine if they are missing.

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

## Interrupting Jobs Safely

Rank 0’s ncurses UI installs a SIGINT handler so it can restore the terminal cleanly. Pressing `Ctrl-C` inside the prompt (or the REPL file field) clears the active line; it does **not** terminate the MPI job. Use `:quit`, `:exit`, `:q`, or press `Esc` if you want to bail out before sending a prompt. After you submit a prompt, `Ctrl-C` in the hosting shell behaves like any other MPI program and stops all ranks immediately; `Ctrl-\` (SIGQUIT) is still available if you need an emergency core dump. For automated flows, run with `--no-tui` / `--stdin` so signals are delivered directly without ncurses in the way.

## CLI Highlights
- `--api-endpoint https://api.deepseek.com/chat/completions`
- `--api-provider openai --model gpt-4o-mini` flips the payload/header layout for OpenAI-compatible JSON APIs (also works with Azure, OpenRouter, etc.).
- `--api-provider anthropic --model claude-3-5-sonnet-20240620 --anthropic-version 2023-06-01` targets Anthropic’s official Claude endpoints.
- `--api-provider zai --model glm-4-plus --api-key-env ZAI_API_KEY` talks to z.ai’s GLM APIs (payload schema matches OpenAI chat completions).
- `--api-key-env API_TOKEN`
- `--chunk-size 4096` (bytes per segment)
- `--input-file payload.txt` or `--stdin`
- `--inline-text "quick payload"` bypasses TUI
- `--repl` keeps the MPI ranks alive in a REPL-style loop so every new prompt includes prior turns
- `--no-tui --readline` switches to a plain GNU Readline prompt; type your payload and finish with a single `.` on its own line
- `--noninteractive --input-file payload.txt --inline-text "Summarize this"` disables TUI/readline entirely and exits immediately if either the input file or inline prompt is missing—ideal for CI scripts that must fail fast
- `--max-retries 5 --retry-delay-ms 750`
- `--network-retries 2` lets each MPI rank tear down and rebuild its HTTP client after transient network failures before giving up on a chunk
- `--timeout 45` (seconds)
- `--max-request-bytes 12288` guardrail for payload growth
- `--max-output-tokens 1024` clamps completion tokens when using OpenAI/Anthropic providers
- `--dry-run` skips HTTP calls but keeps MPI plumbing and logging
- `--response-dir responses/` streams each chunk response into timestamp-free JSON files per rank
- `--response-files / --no-response-files` toggle per-rank response files (default enabled with directory `responses/`)
- `--readline / --no-readline` choose between GNU Readline prompts or plain stdin when the ncurses TUI is disabled
- `--tui-log-view` / `--no-tui-log-view` control the post-prompt ncurses log pane (auto-enabled when `--tui`; auto mode filters chunk/progress spam so you mostly see assistant output, while explicitly passing `--tui-log-view` restores the full log stream)
- `--repl` opens the chat-style ncurses UI (Tab toggles between the file-path field and the prompt, Enter on the file field pulls the file into the buffer, `Ctrl+K` sends the accumulated prompt, and `/help` + `/clear` manage the pending text)
- `--tasks 16` divides text/CSV/Excel payloads into 16 logical slices so MPI ranks keep working sequentially even if there are more tasks than processes
- `--auto-scale-mode chunks --auto-scale-threshold 100000000 --auto-scale-factor 4` splits giant uploads into additional logical tasks; `threads` mode simply logs guidance because MPI rank counts are fixed for the lifetime of the job
- Provider auto-detect kicks in automatically: if your endpoint contains `openai.com`, `anthropic.com`, or `bigmodel.cn`, your env var is `OPENAI_API_KEY` / `ANTHROPIC_API_KEY` / `ZAI_API_KEY`, or your key begins with well-known prefixes such as `sk-ant-`, `sk-claude`, `gk-`, or `glm-`, the client switches to the matching provider so you don’t have to pass `--api-provider`. Use `--api-provider` to override.

Combine options freely; every flag is also available from a simple key/value config file via `--config my.conf` with entries such as `chunk_size=2048` or `api_endpoint=https://api.deepseek.com/...`.

## Workflow Notes
- The application requires an API key in the environment (`DEEPSEEK_API_KEY` by default).
- `OPENAI_API_KEY` and `ANTHROPIC_API_KEY` are auto-selected when you switch providers (override with `--api-key-env` if your endpoint uses a custom variable name).
- Rank 0 hosts the ncurses TUI by default: optionally preload a file path, then type your payload and finish with a single `.` on its own line to send it to all ranks.
- In `--repl` mode the TUI now mirrors a chat-style split window (scrolling output buffer plus dedicated upload/prompt fields). Use Tab to toggle between the upload path and the prompt, Enter to ingest a file (it streams straight into the buffer for MPI chunking), and press `Ctrl+K` to send the accumulated prompt; logs stay in the same pane so the shell never shows raw `mpirun` output mid-session.
- Prefer a terminal REPL? Run `--no-tui --readline` (or `use_tui=false`, `use_readline_prompt=true`) and use the same `.` terminator; logs appear immediately below the prompt.
- Logs default to `deepseek_mpi.log` in the working directory; rotate externally if desired.
- Pass `--log-file /path/to/log` (alias `--log`) to override the MPI log destination and repeat `--verbose` to raise the underlying console verbosity.
- Use `--response-dir` when you need deterministic artifacts for downstream pipelines or compliance.
- Response files are enabled by default (saved under `responses/` per rank/chunk). Disable with `--no-response-files` if you only want log output.
- `--tasks` ensures the entire file (including large spreadsheets) is read once and then auto-sliced, so you’re never limited by the number of hardware threads on the box.
- Autoscaling keeps big drops moving: chunk mode divides payloads across existing ranks; threads mode simply logs a reminder that MPI ranks are fixed, so schedule a larger `-np` yourself if you need more concurrency.
- Provider detection is automatic: endpoints, environment variable names, and well-known key prefixes (Anthropic `sk-ant-`, GLM `gk-`/`glm-`, Azure OpenAI `sk-aoai-`/`sk-az-`, etc.) steer the client toward the right REST API. Explicitly set `--api-provider` if you need to override the heuristic.
- Inside the ncurses prompt, hit `Ctrl+C` to clear the active line, type `:quit`/`:exit`/`:q` to bail out without sending anything, and rely on the REPL-specific `/help` + `/clear` commands when you run with `--repl`.
- The REPL scrollback keeps up to 1024 lines; use Page Up/Down (or Home/End) to browse prior output without leaving the TUI, or enable `--tui-log-view` to mirror logs in a dedicated pane.
- Logs stay inside the ncurses pane by default when `--tui` is active; auto mode hides most mpirun/log noise so only warnings and responses remain, while `--no-tui-log-view` drops back to stdout and `--tui-log-view` explicitly keeps the full log stream.
- Use git for change tracking – a clean history keeps regressions easy to spot.
- When you need a guided UX, run `mpirun -np 4 ./src/deepseek_mpi --repl`. Every time you press `Ctrl+K` the REPL gathers the ongoing conversation, packages it into a payload, and runs inference without spawning a second binary. Type `:quit` or press `Esc` to exit the REPL.
- REPL slash commands are intentionally minimal: `/help` shows shortcuts, `/clear` wipes the staged buffer, `/quit` sets `:quit` for the next submission, and `/np` simply reminds you to restart with a different `-np` if needed.

### REPL File Staging
- Press `Tab` to focus the file-path field, type an absolute/relative path, and hit Enter to append the file contents directly into the prompt buffer (a trailing newline is added automatically if the file does not end with one).
- Large files are pasted verbatim, so be mindful of `--max-request-bytes`—break huge uploads into smaller prompts or rely on `--tasks` so chunking keeps up.
- The chat-style layout dedicates the top pane to prompt/response history, keeps a live REPL prompt at the bottom, and optionally mirrors the most recent MPI log window when `--tui-log-view` is active.

## License
SPDX-License-Identifier: MIT
