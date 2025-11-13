---
description: Build, run, and validate Deepseek MPI from a clean checkout.
---

# Quickstart

Follow these steps to go from zero to a working Deepseek MPI deployment on CentOS 7 (or any comparable Linux distribution).

## 1. Install Dependencies

| Component | Why it’s needed | How to verify |
|-----------|-----------------|---------------|
| GCC/Clang with C11 | Compile the C sources (CentOS 7 ships GCC 4.8+). | `gcc --version` (>= 4.8) |
| OpenMPI or MPICH | Provides `mpicc` and `mpirun` for distributed runs. | `which mpicc && mpicc --showme` |
| libcurl + headers | HTTP client with TLS, retries, compression. | `pkg-config --modversion libcurl` |
| ncurses + headers | Rank 0 ncurses prompt (standard + REPL). | `pkg-config --cflags ncurses` |
| GNU Readline | Interactive prompt when the TUI is disabled. | `pkg-config --modversion readline` |
| Autotools (`autoconf`, `automake`, `libtool`, `pkg-config`) | Generates portable build files. | `autoreconf --version` |
| Doxygen (optional) | API docs (`make doc`). | `doxygen --version` |

> `configure` still probes for `libmagic`, `libxml2`, and `libarchive`. When available, Deepseek MPI can sniff MIME types and extract text from Office/LibreOffice documents before they ever hit the API; without them it falls back to plain-text reads and descriptive base64, so builds still succeed.

Install everything on CentOS 7:

```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y openmpi openmpi-devel libcurl-devel ncurses-devel \
                    readline-devel autoconf automake libtool pkgconfig doxygen \
                    libxml2-devel file-devel libarchive-devel
```

> **Tip:** If you use a custom MPI distribution, ensure its `bin/` directory (with `mpicc`, `mpirun`) is ahead of `/usr/bin` in your `PATH` before running `configure`.

## 2. Bootstrap & Configure

```bash
./autogen.sh                    # or autoreconf -fi
./configure CC=mpicc CFLAGS="-O2 -Wall"
```

- `autogen.sh` (or `autoreconf -fi`) regenerates `configure` and `Makefile.in`.
- Set `CC` to your MPI compiler wrapper (`mpicc`, `hcc`, etc.).
- Pass `--prefix` if you plan to `make install`.
- Optional: `./configure --enable-debug` (custom macro) or override `CFLAGS` such as `-O0 -g -fsanitize=address` for debugging builds.

## 3. Build Everything

```bash
make -j$(nproc)
```

Artifacts appear under `src/` (`deepseek_mpi`) and `doc/` (Doxygen output after `make doc`).

### Build Profiles

| Profile | Flags | When to use |
| --- | --- | --- |
| **Release** | `CFLAGS="-O2 -DNDEBUG"` | Production binaries. |
| **Debug** | `CFLAGS="-O0 -g -fsanitize=address"` | Local debugging, sanitizers. |
| **Hardened** | `CFLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2"` | Security-conscious environments. |

## 4. Run the Client

```bash
export DEEPSEEK_API_KEY="sk-live-..."
mpirun -np 4 ./src/deepseek_mpi \
  --chunk-size 4096 \
  --response-dir responses \
  --network-retries 2 \
  --api-provider deepseek
```

Type your prompt in the ncurses window and finish with a single `.` on its own line. Logs (or `-vv` HTTP traces) stay inside the ncurses pane by default with chunk/progress chatter muted; add `--no-tui-log-view` for the raw stdout stream or pass `--tui-log-view` explicitly if you want the full log flood.

Common flag combos:

- Switch providers: `--api-provider openai --model gpt-4o-mini`, `--api-provider anthropic --anthropic-version 2023-06-01`, or `--api-provider zai --model glm-4-plus`. DeepSeek’s own endpoint (`https://api.deepseek.com/chat/completions`) is OpenAI-compatible, so the default provider works as soon as you export `DEEPSEEK_API_KEY`.
- Run from stdin: `cat payload.txt | mpirun -np 4 ./src/deepseek_mpi --stdin`.
- Non-interactive batching: `--input-file payload.txt --inline-text "Summarize section 2" --noninteractive --show-progress` (fails fast if either input is missing).
- Interactive CLI without ncurses: `--no-tui --readline` (finish with a single `.` line, just like the TUI).
- Autoscaling large payloads: `--auto-scale-mode chunks --auto-scale-threshold 150000000 --auto-scale-factor 4`.

See the [CLI reference](cli.md) for every option.

### Using the REPL TUI

`--repl` keeps `deepseek_mpi` alive between prompts and adds a split-pane UI (history + prompt/file staging):

```bash
mpirun -np 4 ./src/deepseek_mpi --repl \
  --chunk-size 4096 --response-dir responses \
  --tui-log-view --auto-scale-mode chunks --auto-scale-threshold 150000000
```

- Press `Tab` to switch between the file-path field and the prompt; Enter on the file field loads the file and appends its contents to the pending prompt (a newline is added automatically if needed).
- Submit the prompt with `Ctrl+K` (or the traditional lone `.` line). Use `/help` for a cheat sheet, `/clear` to wipe the staged buffer, and `/quit` to enqueue `:quit` without typing it manually.
- `Ctrl+C` clears whichever field is focused, `:quit`/`:exit`/`:q` aborts the capture loop, and `Esc` exits the REPL entirely.
- `--tui-log-view` mirrors the most recent MPI log window inside the REPL; disable it with `--no-tui-log-view` if you prefer raw stdout.

## 5. Validate the Build

An automated test suite hasn’t landed yet (`tests/` is a stub), so `make check` is currently a no-op. Use the manual MPI smoke test below (or wire it into CI) to confirm chunking, MPI broadcast, and response streaming work end-to-end without calling the API.

```bash
mpirun -np 2 ./src/deepseek_mpi --dry-run --inline-text "ping" --auto-scale-mode none
```

Successful output ends with `Cluster summary: processed=2, failures=0, network_failures=0`.

## 6. Generate API Docs (optional)

```bash
make doc
xdg-open doc/html/index.html   # or open … on macOS
```

Doxygen pulls comments from `src/` and installs output under `doc/html`, `doc/latex`, and `doc/xml`.

## 7. Clean Builds

To rebuild from scratch (after deleting generated files or switching branches):

```bash
git clean -fdx         # optional, destructive
./autogen.sh
./configure CC=mpicc
make
```

Or skip `autogen.sh` and run `autoreconf -fi` followed by the usual `configure && make`.

## 8. Next Steps

- Dive into the [Configuration guide](configuration.md) to tune retries, chunk sizing, providers, and config files.
- Spend time in the built-in `--repl` UI when you want to keep MPI ranks alive between prompts while staging files inline.
- Publish these docs on GitBook by following the [GitBook guide](gitbook.md).
- Review the [Operations guide](operations.md) to plan logging, monitoring, and autoscaling policies before your first production rollout.
