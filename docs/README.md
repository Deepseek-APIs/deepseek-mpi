# Deepseek MPI Docs

Welcome to the Deepseek MPI documentation hub. This GitBook-ready tree mirrors and expands on the repository README so every topic gets its own focused guide.

## What You’ll Find

- [Quickstart](quickstart.md) – clean-room builds, dependency setup, execution, tests, and Doxygen tips.
- [CLI Reference](cli.md) – exhaustive flag tables with usage snippets and advanced examples.
- [Configuration](configuration.md) – environment variables, autoscaling, response capture, and resiliency tuning.
- [Operations](operations.md) – day‑2 guidance covering logging, troubleshooting, autoscaling strategies, and wrapper UX.
- [GitBook Publishing](gitbook.md) – how to connect this repo to GitBook, polish the space, and keep it synced.

Each page intentionally contains fully fleshed-out instructions so they render well on GitBook. You can extend the tree with additional Markdown files (e.g., “Release Notes” or “API Reference”) and register them in `SUMMARY.md`.

## Architecture Snapshot

Deepseek MPI is composed of a few well-defined layers:

| Layer | Responsibility |
| --- | --- |
| `deepseek_mpi` core | Parses CLI/config, builds payloads, slices input, and coordinates MPI ranks. |
| `api_client` | Owns libcurl handles, retries, compression, and provider-specific payloads. |
| `tui` / `readline_prompt` | Capture payload content interactively (ncurses or GNU Readline). |
| `deepseek_wrapper` | Optional ncurses REPL that shells out to `mpirun` and manages attachments. |
| `docs/` | GitBook-ready Markdown, synced directly from `main`. |

Understanding these layers helps when you extend the docs—each guide can focus on a single component.

## How to Navigate

- The GitBook sidebar is generated from [`SUMMARY.md`](SUMMARY.md). Edit that file to reorder chapters or create nested sections.
- Use the breadcrumb buttons GitBook adds automatically to jump between pages or edit the source on GitHub.
- Cross-link liberally (e.g., `[See Configuration](configuration.md#autoscaling)`); GitBook automatically rewrites the paths.

## Authoring Workflow

1. Edit or create Markdown files under `/docs`.
2. Update `SUMMARY.md` if you add a new page or subsection.
3. Preview locally (`grip docs/README.md`, VS Code preview, etc.).
4. Submit a PR; once merged, GitBook’s “Sync on push” (if enabled) pulls the latest docs automatically.

### Style Checklist

- Use sentence case headings, prefer fenced code blocks with explicit languages, and keep tables simple.
- Wrap CLI flags and environment variables in backticks; highlight files with relative paths (e.g., ``src/main.c``).
- Keep paragraphs short (GitBook treats blank lines as paragraph breaks).
- When embedding shell commands, include context (e.g., “run from repo root”).

### Adding New Pages

```
docs/
  new-topic.md
SUMMARY.md   # add "* [New Topic](new-topic.md)"
```

GitBook will pick up the page on the next sync. For deeply nested content, indent entries in `SUMMARY.md`:

```
* [Operations](operations.md)
  * [Monitoring](operations.md#monitoring-and-alerting)
  * [Autoscaling](operations.md#autoscaling-runbooks)
```

## Release & Docs Workflow

| Step | Action |
| --- | --- |
| 1 | Implement features/fixes. |
| 2 | Update or add docs (usually under `/docs`). |
| 3 | Run `markdownlint` or your preferred linter if available. |
| 4 | Commit code + docs together. |
| 5 | Merge to `main`; GitBook syncs automatically (or run a manual sync). |

Treat docs as a first-class artifact—every change shipped to users should include the relevant documentation snippet or link so GitBook remains authoritative.
