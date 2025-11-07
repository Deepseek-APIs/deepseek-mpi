---
description: Build, run, and validate Deepseek MPI from a clean checkout.
---

# Quickstart

Follow these steps to go from zero to a working Deepseek MPI deployment on CentOS 7 (or any comparable Linux distribution).

## 1. Install Dependencies

| Component | Why it’s needed |
|-----------|-----------------|
| GCC/Clang with C11 | Compile the C sources (CentOS 7 ships GCC 4.8+). |
| OpenMPI or MPICH | Provides `mpicc` and `mpirun` for distributed runs. |
| libcurl + headers | HTTP client with TLS, retries, compression. |
| ncurses + headers | TUI and wrapper UI. |
| Autotools (`autoconf`, `automake`, `libtool`, `pkg-config`) | Generates portable build files. |
| Optional: `doxygen`, `libxml2`, `libmagic`, `libarchive` | API docs + richer file ingestion in `deepseek_wrapper`. |

Install everything on CentOS 7:

```bash
sudo yum groupinstall -y "Development Tools"
sudo yum install -y openmpi openmpi-devel libcurl-devel ncurses-devel \
                    autoconf automake libtool pkgconfig doxygen \
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

## 3. Build Everything

```bash
make -j$(nproc)
```

Artifacts appear under `src/` (`deepseek_mpi`, `deepseek_wrapper`) and `doc/` (Doxygen output after `make doc`).

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

- Switch providers: `--api-provider openai --model gpt-4o-mini`, or `--api-provider anthropic --anthropic-version 2023-06-01`.
- Run from stdin: `cat payload.txt | mpirun -np 4 ./src/deepseek_mpi --stdin`.
- Non-interactive batching: `--input-file payload.txt --no-tui --show-progress`.

See the [CLI reference](cli.md) for every option.

## 5. Validate with Tests

The project ships with an Automake-based unit test suite (no Boost requirement):

```bash
make check
```

This builds/runs:

- `unit_core_tests` – config defaults, chunk scheduling, string buffer math, file loader.
- `cli_tests` – flag parsing, config-file ingestion, minimum guards (chunk size, retries, network budgets).

CI-friendly: include `make check` in your pipeline to guard against regressions.

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
