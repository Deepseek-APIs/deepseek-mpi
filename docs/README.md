# Deepseek MPI Docs

Welcome to the Deepseek MPI documentation hub. This GitBook-ready tree mirrors and expands on the repository README so every topic gets its own focused guide.

## What You’ll Find

- [Quickstart](quickstart.md) – clean-room builds, dependency setup, execution, tests, and Doxygen.
- [CLI Reference](cli.md) – exhaustive flag tables with usage snippets.
- [Configuration](configuration.md) – environment variables, config files, default values, and resiliency tuning.
- [GitBook Publishing](gitbook.md) – how to connect this repo to GitBook, polish the space, and keep it synced.

Each page intentionally contains fully fleshed-out instructions so they render well on GitBook. You can extend the tree with additional Markdown files (e.g., “Wrapper UX” or “API Reference”) and register them in `SUMMARY.md`.

## How to Navigate

- The GitBook sidebar is generated from [`SUMMARY.md`](../SUMMARY.md). Edit that file to reorder chapters or create nested sections.
- Use the breadcrumb buttons GitBook adds automatically to jump between pages or edit the source on GitHub.

## Contributing to the Docs

1. Edit or create Markdown files under `/docs`.
2. Update `SUMMARY.md` if you add a new page.
3. Run a quick Markdown preview locally (any editor or `grip`, `mdcat`, etc.).
4. Submit a PR; once merged, GitBook’s “Sync on push” (if enabled) pulls the latest docs automatically.

For style consistency:

- Use sentence case headings, prefer fenced code blocks with explicit languages, and keep tables simple.
- When referencing CLI flags, wrap them in backticks; highlight files with relative paths.
- Link to other pages using relative paths so GitBook can rewrite them automatically.

Need more pages? Duplicate this structure:

```
docs/
  new-topic.md
SUMMARY.md   # add "* [New Topic](docs/new-topic.md)"
```

That’s it—GitBook will include the new page on the next sync.
