# GitBook Publishing Guide

Use this playbook to publish the Deepseek MPI docs on GitBook, keep them synced with `main`, and ensure contributors know how to update the content.

## 1. Prepare the Repository

1. All GitBook-ready Markdown lives under `/docs` (see [`docs/README.md`](README.md), [`quickstart.md`](quickstart.md), [`cli.md`](cli.md), [`configuration.md`](configuration.md)).
2. The repo root contains `SUMMARY.md`, which GitBook uses as the sidebar/table of contents.
3. Keep the root `README.md` concise and link to GitBook so GitHub visitors know where to find the polished docs.

## 2. Connect GitBook to GitHub

1. Sign in to GitBook and create (or select) the organization that will host the docs.
2. Create a new **Space** → choose **Import from GitHub**.
3. Authorize GitBook to access `Deepseek-APIs/deepseek-mpi`.
4. Select the repository and specify:
   - Branch: `main`
   - Docs path: `/` (GitBook automatically detects `SUMMARY.md` and `/docs`)
   - Import mode: automatic sync (recommended) or manual.
5. Finish the import; GitBook builds the sidebar from `SUMMARY.md`.

## 3. Polish the Content

After the first import, customize the space:

- Add hero/cover art, tag the space as “Public”, and toggle sharing.
- Configure code block themes, typography, and search behavior in GitBook settings.
- If you need additional pages (e.g., release notes, API references), create them in `/docs` and update `SUMMARY.md`.
- Use GitBook’s “Edit on GitHub” buttons so contributors can jump straight to the source file.

## 4. Keep Docs in Sync

To avoid drift between GitHub and GitBook:

1. **Automatic Sync:** In the GitBook space settings → Integrations → GitHub, enable “Sync on push”. GitBook will pull whenever `main` changes.
2. **GitHub Actions (optional):** You can add a workflow that validates docs before merges, e.g., ensuring `SUMMARY.md` references valid files or running `markdownlint`. Example `.github/workflows/docs.yml` snippet:

   ```yaml
   name: Docs Lint
   on:
     pull_request:
       paths:
         - 'docs/**'
         - 'SUMMARY.md'
   jobs:
     markdownlint:
       runs-on: ubuntu-latest
       steps:
         - uses: actions/checkout@v4
         - uses: DavidAnson/markdownlint-cli2-action@v15
           with:
             globs: 'docs/**/*.md SUMMARY.md'
   ```

3. **Contributor Guide:** In `CONTRIBUTING.md` (or the README), note that any substantial docs change should accompany GitBook updates. Mention `docs/gitbook.md` so new contributors follow the same process.

## 5. Publishing Checklist

- [ ] `/docs` contains the latest Markdown and images.
- [ ] `SUMMARY.md` reflects the desired sidebar order.
- [ ] GitBook space is set to **Public** with the correct slug (e.g., `https://deepseek-apis.gitbook.io/deepseek-mpi/`).
- [ ] “Sync on push” is enabled, or a recurring manual sync schedule is documented.
- [ ] Optional: use custom domains or GitBook’s built-in domain for branding.

With this setup, every merge to `main` automatically updates GitBook, ensuring the public docs always match the released code.
