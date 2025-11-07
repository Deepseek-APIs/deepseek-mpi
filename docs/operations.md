# Operations Guide

This page covers the “day‑2” tasks you’ll tackle after Deepseek MPI is deployed: observing health, tuning autoscaling, managing attachments, and troubleshooting failures across ranks.

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
| Wrapper attachments | According to your upstream policy | Attachments may include PII—treat accordingly. |

### Structured Log Fields

Every line includes: timestamp, level, `rank`, and the message. For example:

```
[2024-07-02 10:15:01] INFO [rank 3] | Chunk 12 (4096 bytes) succeeded
```

Use `logger.process_rank` and `logger.verbosity` in custom tooling if you extend the codebase.

## Autoscaling Runbooks

Deepseek MPI has two autoscaling layers:

1. **Core autoscaling (`--auto-scale-mode`)** – modifies logical tasks/chunk sizing when payloads exceed `--auto-scale-threshold`. Modes: `none`, `chunks`, `threads` (threads mode logs guidance because rank counts are fixed once `mpirun` starts).
2. **Wrapper autoscaling (`deepseek_wrapper`)** – can increase `mpirun -np` or task count before launching each job. Use flags `--auto-scale-mode threads --auto-scale-factor 2` plus `--auto-scale-max-np` to cap concurrency.

### Sample Policy

| Payload Size | Action |
| --- | --- |
| `<50 MB` | No change (baseline `np=4`, `tasks=16`). |
| `50–150 MB` | Wrapper doubles task count: `--auto-scale-mode chunks --auto-scale-factor 2`. |
| `>150 MB` | Wrapper doubles MPI ranks (if cluster capacity allows) and sets `tasks=np*2`. |

Tune thresholds per workload; monitor wall-clock time per job to refine factors.

## Wrapper UX & Attachments

`deepseek_wrapper` provides a ncurses REPL plus a payload staging area.

- `/attach <path>` inspects the file with `libmagic` and `libarchive`. Text attachments are inlined; binary attachments are base64-encoded with metadata.
- Slash commands: `/np`, `/tasks`, `/chunk`, `/dry-run`, `/help`, `/clear`.
- Every prompt run spawns `mpirun -np <np> ...` and streams stdout/stderr to the “Last Output” pane, also captured to `last_output` buffer for copy/paste.

### Attachment Limits

| Type | Notes |
| --- | --- |
| Text | First 16 KB is included inline; remainder gets a `... [truncated]` marker. |
| Binary | Encoded entirely; beware of introducing huge base64 payloads—use chunking or store externally if >5 MB. |
| Archives | If `libarchive` is available, `.zip`/`.tar.gz` contents are listed before encoding. |

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
| Wrapper attachments not showing | Missing optional libs | Install `libmagic`, `libxml2`, `libarchive` (headers + runtime). |
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
