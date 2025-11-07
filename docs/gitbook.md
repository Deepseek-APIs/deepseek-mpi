# GitBook Publishing Guide

Use this playbook to publish the Deepseek MPI docs on GitBook, keep them synced with `main`, and ensure contributors know how to update the content.

## 1. Prepare the Repository

1. All GitBook-ready Markdown lives under `/docs` (see [`docs/README.md`](README.md), [`quickstart.md`](quickstart.md), [`cli.md`](cli.md), [`configuration.md`](configuration.md), [`operations.md`](operations.md)).
2. [`docs/SUMMARY.md`](SUMMARY.md) defines the sidebar. Keep it in sync with the Markdown files you add or remove.
3. The root `README.md` links to the live GitBook space (`https://deepseek-apis.gitbook.io/deepseek-mpi`), so GitHub visitors always find the canonical docs.

## 2. Connect GitBook to GitHub

1. Sign in to GitBook and select the organization that will host the docs (e.g., `deepseek-apis`).
2. Create a new **Space** → choose **Import from GitHub**.
3. Authorize GitBook to access `Deepseek-APIs/deepseek-mpi`.
4. Select the repository and specify:
   - Branch: `main`
   - Root: `/docs` (GitBook detects `SUMMARY.md` automatically)
   - Import mode: automatic sync (recommended) or manual.
5. Finish the import; GitBook builds the sidebar from `docs/SUMMARY.md`.

## 3. Polish the Space

After the first import, customize the space:

- **Branding:** Add cover art, favicon, and update the short description.
- **Slug:** Space settings → General → Slug. Set it to `deepseek-mpi` to align with the README link.
- **Visibility:** Make the space **Public** unless you are on a private plan.
- **Analytics:** Connect to Google Analytics or similar (paid plans) if you need traffic data.
- **Custom domain (optional):** Under Settings → Domain, add `docs.deepseek-apis.com` (requires paid plan). Until then, `https://deepseek-apis.gitbook.io/deepseek-mpi` remains the canonical URL.

## 4. Keep Docs in Sync

### Automatic Sync

1. In the GitBook space, open **Integrations → GitHub**.
2. Enable “Sync on push” and select the branch (`main`).
3. GitBook now listens for pushes and pulls the latest Markdown automatically.

### Manual Sync (fallback)

If automatic sync is disabled, click **Sync → Pull from Git** in the space whenever docs change. This is useful in locked-down environments.

### CI/CD Guardrails

Optionally lint docs on pull requests:

```yaml
name: Docs Lint
on:
  pull_request:
    paths:
      - 'docs/**'
      - 'docs/SUMMARY.md'
jobs:
  markdownlint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: DavidAnson/markdownlint-cli2-action@v15
        with:
          globs: 'docs/**/*.md docs/SUMMARY.md'
```

## 5. Advanced Tips

- **Versioned docs:** GitBook supports multiple branches/spaces. Clone the space (e.g., “v0.1”, “main”) and point each at a different branch.
- **Edit on GitHub buttons:** Enable under Settings → GitHub to add “Edit on GitHub” links to every page.
- **Embed diagrams:** Store `.drawio` or `.svg` files in `/docs/assets/` and reference them relatively; GitBook inlines them automatically.
- **Search metadata:** Add front-matter (`--- description: ... ---`) to improve GitBook search relevance (already present in several files).

## 6. Troubleshooting

| Issue | Fix |
| --- | --- |
| Space keeps importing the root `README.md` instead of `/docs` | Ensure `.gitbook.yaml` (if used) points `root: ./docs`, or set **Root folder** in the GitBook integration to `/docs`. |
| GitBook fails to sync because of merge conflicts | Rebase/merge PRs on GitHub first; GitBook only pulls fast-forward commits. |
| Sidebar shows stale entries | Run `git grep 'SUMMARY' -n` to ensure `docs/SUMMARY.md` matches the files on disk, then re-sync. |
| URL should use new org slug | Rename the GitBook organization (Settings → General) or map a custom domain. |

## 7. Publishing Checklist

- [ ] `/docs` contains the latest Markdown and assets.
- [ ] `docs/SUMMARY.md` matches the desired sidebar order and nesting.
- [ ] GitBook space visibility is set correctly (Public/Private).
- [ ] “Sync on push” is enabled, or your team has a manual sync cadence.
- [ ] README links to the live space and the GitHub “Website” metadata points to it.

With this setup, every merge to `main` automatically updates GitBook, ensuring the public docs always match the released code.
