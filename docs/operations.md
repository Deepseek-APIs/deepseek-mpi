# Operations Guide

This page covers the “day‑2” tasks you’ll tackle after Deepseek MPI is deployed: observing health, tuning autoscaling, managing the REPL/TUI workflow, and troubleshooting failures across ranks.

## Logging & Observability

### Log Destinations

- **Stdout** – every rank mirrors log lines to stdout by default (`logger.mirror_stdout = true`). Aggregators such as Fluent Bit or CloudWatch can scrape `mpirun` output directly.
- **Log files** – set `--log-file /var/log/deepseek/rank.log` to append rank-specific logs. Pair with logrotate or systemd-journald for retention.
- **Response artifacts** – enable `--response-dir responses/` (or `response_dir=/path` inside config files) to persist JSON per chunk: `chunk-000123-r2.json`.

### Recommended Retention Policy

| Data | Retain | Reason |
| --- | --- | --- |
| `deepseek_mpi.log` | 7–14 days | Postmortems and usage insights. |
| `responses/*.json` | 30–90 days (if containing user data) | Compliance/auditing; purge sooner if privacy requires. |

### Structured Log Fields

Every line includes: timestamp, level, `rank`, and the message. For example:

```
[2024-07-02 10:15:01] INFO [rank 3] | Chunk 12 (4096 bytes) succeeded
```

Use `logger.process_rank` and `logger.verbosity` in custom tooling if you extend the codebase.

## Autoscaling Runbooks

Deepseek MPI’s autoscaler lives entirely inside the core binary. When `--auto-scale-mode=chunks`, rank 0 increases the logical task count (and therefore reduces each chunk’s size) once the prompt exceeds `--auto-scale-threshold`. `--auto-scale-mode=threads` simply logs a reminder that MPI ranks are fixed for the lifetime of a job—scale `mpirun -np` yourself through your scheduler or wrapper scripts when you need more concurrency.

### Sample Policy

| Payload Size | Action |
| --- | --- |
| `<50 MB` | No change (baseline `np=4`, `tasks=16`). |
| `50–150 MB` | Enable chunks autoscaling: `--auto-scale-mode chunks --auto-scale-factor 2`. |
| `>150 MB` | Launch `mpirun` with a higher `-np` (e.g., 8) *and* keep chunks autoscaling enabled so the larger rank count stays busy. |

Tune thresholds per workload; monitor wall-clock time per job to refine factors. Remember that increasing `--tasks`/`--mp` (or the legacy `--np`) or enabling chunks autoscaling reduces chunk size but does not change the MPI world size—you must restart the job with a higher `-np` (via your scheduler or orchestration layer) if you need more ranks.

## Interactive REPL UX

Run `mpirun ... ./src/deepseek_mpi --repl` when you need a chat-style workflow without leaving the main binary.

- Tab switches focus between the file-path input and the prompt. Enter on the file field immediately reads the file (relative or absolute path) and appends its contents to the staged prompt; a newline is added automatically if the file does not end with one.
- `Ctrl+K` submits the current prompt. The classic `.` on its own line still works when you want a quick send without leaving the keyboard home row.
- `/help` prints the shortcut list, `/clear` wipes the staged buffer, and `/quit` enqueues `:quit` if you need to bail without sending.
- `Ctrl+C` clears whichever field currently has focus. Type `:quit`, `:exit`, `:q`, or press `Esc` to exit the REPL without submitting.
- Enable `--tui-log-view` to mirror the most recent MPI logs inside the REPL window. Disable it (`--no-tui-log-view`) if you would rather stream stdout/stderr back to the shell.
- `--repl-history N` bounds how many prior turns are resent in each request (default `4`, set to `0` for unlimited context) so you can keep scrollback visible without paying for infinite prompts.

### File Staging Tips

- Input files are MIME-sniffed before staging: plain text/code goes in verbatim, Office/LibreOffice archives are unpacked, PDFs are rendered + OCR’d via Tesseract (when available), and opaque binaries are summarized via base64. You still need to monitor `--max-request-bytes` (and consider `--tasks`/`--mp` or chunk autoscaling) before dropping massive blobs into the REPL.
- Set `TESSERACT_LANG=XXX` (defaults to `eng`) if you need OCR in another language; the variable is read whenever the PDF pipeline is invoked.
- Use a scratch buffer if you need to merge multiple files: load each path sequentially, edit the combined prompt in-place, then hit `Ctrl+K` once you’re satisfied.
- Remember that staged prompts live only on rank 0 until you submit—you can press `/clear` or `Ctrl+C` repeatedly without touching previously submitted history.

### Signal Handling & Cancellation

Rank 0’s ncurses UI traps SIGINT so it can clean up the terminal. That means `Ctrl-C` clears the active field instead of terminating the program. Practical implications:

- Use `Ctrl-C` to wipe the current line, and type `:quit`, `:exit`, `:q`, or press `Esc` if you want to abandon the REPL/TUI without sending anything.
- After you submit a prompt, `Ctrl-C` in the hosting terminal behaves like any other MPI job and stops every rank immediately. `Ctrl-\` (SIGQUIT) is still available if you need an emergency core dump.
- For automation or headless runs, prefer `--no-tui --stdin`, `--readline`, or `--noninteractive --input-file ... --inline-text ...` so signals are delivered directly to `deepseek_mpi`.
- If the REPL ever becomes unresponsive, kill the `mpirun` process (or send `pkill -TERM deepseek_mpi`); response files and logs are flushed incrementally, so you won’t lose completed chunks.

## Monitoring & Alerting

Key metrics to watch (exposed via logs today; integrate with your observability stack):

- **Chunk throughput** – `processed` counts per summary log on rank 0.
- **Failures vs. network_failures** – spikes in `network_failures` often indicate TLS/firewall issues.
- **Latency per chunk** – annotate logs or wrap `mpirun` with `/usr/bin/time -v` to capture runtime.
- **Queue depth** – if you integrate with job schedulers (PBS/Slurm), track pending Deepseek MPI jobs to decide when to autoscale worker pools.

Consider piping logs into Prometheus or Datadog by tailing stdout/stderr and parsing the consistent format.

## Troubleshooting Checklist

| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| `mpi.h is required` during configure | Missing MPI headers | `sudo yum install openmpi openmpi-devel` and ensure `mpicc` is on `PATH`. |
| Immediate failure with `mpi_broadcast` errors | Mixed MPI versions between nodes | Align MPI runtime + compiler across cluster; avoid mixing OpenMPI and MPICH in the same run. |
| HTTP 401/403 responses | Wrong API key or provider mismatch | Verify `--api-provider`/`--model` pair and the env var set via `--api-key-env`. |
| `network_failures` climbing above zero | Flaky network or TLS MITM | Bump `--network-retries`, inspect firewall/SSL inspection devices, verify CA bundle. |
| TUI crashes in non-interactive shells | Trying to run ncurses without a TTY | Use `--no-tui --readline` or `--stdin`. |

## Incident Response Template

1. Capture failing `mpirun` command, including flags and payload size.
2. Collect logs from rank 0 (`deepseek_mpi.log` or console output).
3. Note API provider + endpoint.
4. Check `network_failures` counts and HTTP status codes.
5. Reproduce with `--dry-run` to verify parsing/chunking without hitting the API.
6. File a GitHub issue referencing commits/logs; include anonymized payload snippets if possible.

## Hardening Tips

- Run `deepseek_mpi` inside a dedicated service account with least privilege on shared clusters.
- Restrict outbound egress to your approved API endpoints; use `--api-endpoint` overrides when hitting region-specific hosts.
- Rotate API keys regularly; config files should reference env variables instead of inline secrets.
- Store response artifacts on encrypted disks if they contain sensitive user data.

Use this page as the operational playbook—extend it with organization-specific SOPs, runbooks, or integration notes as you productionize Deepseek MPI.
