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
| ncurses + headers | TUI and wrapper UI. | `pkg-config --cflags ncurses` |
| GNU Readline | Interactive prompt when the TUI is disabled. | `pkg-config --modversion readline` |
| Autotools (`autoconf`, `automake`, `libtool`, `pkg-config`) | Generates portable build files. | `autoreconf --version` |
| Optional: `doxygen`, `libxml2`, `libmagic`, `libarchive` | API docs + richer file ingestion in `deepseek_wrapper`. | `pkg-config --list-all | grep libmagic` |

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

Artifacts appear under `src/` (`deepseek_mpi`, `deepseek_wrapper`) and `doc/` (Doxygen output after `make doc`).

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

Common flag combos:

- Switch providers: `--api-provider openai --model gpt-4o-mini`, `--api-provider anthropic --anthropic-version 2023-06-01`, or `--api-provider zai --model glm-4-plus`.
- Run from stdin: `cat payload.txt | mpirun -np 4 ./src/deepseek_mpi --stdin`.
- Non-interactive batching: `--input-file payload.txt --no-tui --show-progress`.
- Interactive CLI without ncurses: `--no-tui --readline`.
- Autoscaling large payloads: `--auto-scale-mode chunks --auto-scale-threshold 150000000 --auto-scale-factor 4`.

See the [CLI reference](cli.md) for every option.

### Using the Wrapper

For an ncurses-driven experience that shells out to MPI per prompt:

```bash
./src/deepseek_wrapper --np 4 --binary ./src/deepseek_mpi \
  --chunk-size 4096 --response-dir responses \
  --auto-scale-mode threads --auto-scale-threshold 150000000
```

- Slash commands (`/np`, `/tasks`, `/chunk`, `/dry-run`, `/attach`) mirror CLI flags.
- Attachments are base64-encoded if binary; text attachments append inline (with `[truncated]` markers when >16 KB).

## 5. Validate with Tests

The project ships with an Automake-based unit test suite (no Boost requirement):

```bash
make check
```

This builds/runs:

- `unit_core_tests` – config defaults, chunk scheduling, string buffer math, file loader.
- `cli_tests` – flag parsing, config-file ingestion, minimum guards (chunk size, retries, network budgets).
- `wrapper_tests` – attachment parsing and slash-command plumbing (skipped when optional libs are absent).

CI-friendly: include `make check` in your pipeline to guard against regressions.

### MPI Smoke Test

After building, run a dry distributed test to ensure ranks can communicate:

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
- Familiarize yourself with the [`deepseek_wrapper`](../src/deepseek_wrapper.c) if you need a Codex-style REPL.
- Publish these docs on GitBook by following the [GitBook guide](gitbook.md).
- Review the [Operations guide](operations.md) to plan logging, monitoring, and autoscaling policies before your first production rollout.
