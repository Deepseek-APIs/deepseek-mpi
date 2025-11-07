# deepseek-mpi

Distributed TUI client for the DeepSeek Platform API. The utility slices very large text inputs, pushes the chunks to DeepSeek in parallel with MPI ranks, and mirrors results to both the console and a rotating log file. CentOS 7 users can rely on Autotools for predictable builds, while Doxygen keeps the codebase documented.

## Features
- **MPI-driven parallelism** – each rank owns a disjoint set of chunks so API traffic scales horizontally.
- **Ncurses TUI** – interactive session for pasting text or attaching a file by path; can be disabled for batched usage.
- **Configurable CLI** – tune endpoints, chunk sizing, retry windows, logging, stdin/input sources, and more.
- **Robust HTTP client** – libcurl with exponential backoff and deterministic JSON payloads.
- **Logging** – mirrored console + file logging with rank annotations for easy traceability.
- **Doxygen docs** – `make doc` renders browsable API docs in `doc/html`.
- **Response archival** – optional `--response-dir` switch persists every chunk response as JSON for audit trails.
- **Deepseek wrapper** – launch `deepseek_wrapper` for a conversational ncurses experience (Codex-like) that automatically spawns MPI jobs.

## Requirements
- GCC or Clang (CentOS 7 ships GCC 4.8+)
- OpenMPI or MPICH providing `mpicc`
- libcurl
- ncurses
- Autotools toolchain (autoconf, automake, libtool)
- Doxygen (optional but recommended for docs)

Install dependencies on CentOS 7 (adjust for your MPI flavor):

```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y openmpi openmpi-devel libcurl-devel ncurses-devel autoconf automake libtool doxygen
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
- `--api-key-env API_TOKEN`
- `--chunk-size 4096` (bytes per segment)
- `--input-file payload.txt` or `--stdin`
- `--inline-text "quick payload"` bypasses TUI
- `--no-tui` forces batch flow
- `--max-retries 5 --retry-delay-ms 750`
- `--timeout 45` (seconds)
- `--max-request-bytes 12288` guardrail for payload growth
- `--dry-run` skips HTTP calls but keeps MPI plumbing and logging
- `--response-dir responses/` streams each chunk response into timestamp-free JSON files per rank
- `deepseek_wrapper --np 4` opens a chat-style interface and shells out to `mpirun` for every prompt
- `--tasks 16` divides text/CSV/Excel payloads into 16 logical slices so MPI ranks keep working sequentially even if there are more tasks than processes

Combine options freely; every flag is also available from a simple key/value config file via `--config my.conf` with entries such as `chunk_size=2048` or `api_endpoint=https://api.deepseek.com/...`.

## Workflow Notes
- The application requires an API key in the environment (`DEEPSEEK_API_KEY` by default).
- Rank 0 hosts the TUI; non-root ranks wait for the broadcast payload.
- Logs default to `deepseek_mpi.log` in the working directory; rotate externally if desired.
- Use `--response-dir` when you need deterministic artifacts for downstream pipelines or compliance.
- `--tasks` ensures the entire file (including large spreadsheets) is read once and then auto-sliced, so you’re never limited by the number of hardware threads on the box.
- Use the `deepseek_wrapper` helper when you want something closer to the OpenAI Codex UX; it logs MPI output in-line and reuses the same configuration flags under the hood.
- Use git for change tracking – a clean history keeps regressions easy to spot.
- When you need a guided UX, run `./src/deepseek_wrapper --np 4 --binary ./src/deepseek_mpi`. Every time you hit Enter the wrapper gathers the ongoing conversation, writes a payload file, and invokes `mpirun` with the requested rank count. Type `:quit` or press `Esc` to exit.

## License
SPDX-License-Identifier: MIT
