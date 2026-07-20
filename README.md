# ReBirth RB-338 Community Archive

A community-maintained archive for the legendary **ReBirth RB-338** software synthesizer
by Propellerhead Software — preserving songs, mods, and history from 1997 through 2.0.1.

Built with [Astro](https://astro.build).

---

## Table of Contents

- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Contributing Files](#contributing-files)
  - [Contributing .rbs Song Files](#contributing-rbs-song-files)
  - [Contributing .rbm Mod Files](#contributing-rbm-mod-files)
- [Contributing Documentation](#contributing-documentation)
- [Archive Indexers](#archive-indexers)
- [WebAssembly Audio Module](#webassembly-audio-module)
- [Deployment](#deployment)
- [Continuous Integration](#continuous-integration)
- [Code Style & Standards](#code-style--standards)
- [License](#license)

---

## Getting Started

### Prerequisites

- **Node.js** 22.x (CI target) or ≥ 18.17.1 — [download](https://nodejs.org/)
- **npm** ≥ 9 (bundled with Node.js)

### Install dependencies

```bash
npm install
```

### Run the development server

```bash
npm run dev
```

The site will be available at **http://localhost:4321** by default.
Astro automatically reloads the page when you edit source files.

### Other commands

| Command           | Description                                       |
|-------------------|---------------------------------------------------|
| `npm run dev`     | Start local dev server at `localhost:4321/rb338/` |
| `npm run build`   | Build the production site into `dist/`            |
| `npm run preview` | Preview the production build locally              |
| `npm run check`   | Astro + TypeScript type check (`astro check`)     |
| `npm run lint`    | ESLint for Astro/TS sources                       |
| `npm run format`  | Prettier check (Astro/TS/JSON/CSS)                |
| `npm run astro`   | Run Astro CLI directly                            |

---

## Project Structure

```
/
├── public/
│   ├── archive/
│   │   ├── rbs-songs/          ← .rbs song files (hosted externally; .gitkeep only)
│   │   └── rbm-mods/           ← .rbm mod files (hosted externally; .gitkeep only)
│   ├── styles/
│   │   └── rebirth-theme.css   ← Single source of truth for global CSS
│   └── rbs-manifest.json       ← Archive metadata index
├── src/
│   ├── components/
│   │   ├── archive/            ← ArchivePageShell, ArchiveEmptyState, ArchiveLink
│   │   ├── ui/                 ← HardwarePanel, Lcd, Led, Knob, ActionButton, …
│   │   ├── Breadcrumbs.astro
│   │   ├── ModCard.astro
│   │   ├── ModBrowserCard.astro
│   │   ├── CollectionCard.astro
│   │   ├── RbsPlayer.astro
│   │   └── ExternalLink.astro
│   ├── content/
│   │   └── docs/               ← Historical docs (v1.0 → v2.0.1)
│   ├── data/
│   │   ├── mods-metadata.json
│   │   ├── mods-full-index.json
│   │   ├── songs-full-index.json
│   │   └── archive-stats.ts
│   ├── layouts/
│   │   └── BaseLayout.astro    ← Shared HTML shell (links rebirth-theme.css)
│   ├── lib/
│   │   └── url.ts              ← normalizeBase() for internal links
│   ├── pages/
│   │   ├── index.astro         ← Landing page
│   │   ├── history.astro       ← Deep-dive history essay
│   │   ├── rbs-archive.astro   ← Redirect → /archive/songs
│   │   ├── docs/               ← /docs content collection routes
│   │   └── archive/
│   │       ├── songs.astro     ← /archive/songs
│   │       ├── songs/[...slug].astro
│   │       └── mods.astro      ← /archive/mods
│   ├── styles/
│   │   └── global.css          ← Layering pointer (not imported; see rebirth-theme.css)
│   └── wasm/
│       ├── audio-module.config.ts  ← WASM runtime paths + feature flags
│       ├── js/                     ← WasmAudioBridge, DegradedRbsPlayer, …
│       ├── types/wasm-audio.ts
│       ├── cpp/                    ← Emscripten C++ engine (build with npm run wasm:build)
│       └── README.md
├── scripts/                    ← Python indexers and upload helpers
├── tests/                        ← Playwright E2E specs
├── astro.config.mjs
├── package.json
└── tsconfig.json
```

---

## Contributing Files

### Contributing `.rbs` Song Files

1. **Fork** the repository and create a branch: `git checkout -b add/my-song-name`
2. Place your `.rbs` file in `public/archive/rbs-songs/`
3. Use a descriptive, lowercase, hyphen-separated filename:
   ```
   acid-bassline-140bpm.rbs
   industrial-pattern-set-01.rbs
   ```
4. If the file is hosted remotely, re-run `python3 scripts/index-rbs-archive.py` so the
   folder detail page picks it up.
5. Open a **Pull Request** against the `main` branch with a brief description of the
   song, the approximate date it was created, and any software/hardware context.

**Accepted formats:** `.rbs` (ReBirth Song File, all versions)

### Contributing `.rbm` Mod Files

Most of the 367 archived mods still lack metadata. See [`docs/CONTRIBUTING-MODS.md`](docs/CONTRIBUTING-MODS.md) for the full JSON schema, tag conventions, and workflow.

Quick path:

1. Find an undocumented mod on [`/archive/mods`](https://ford442.github.io/rb338/archive/mods).
2. Click **“Help document this mod”** to open a pre-filled GitHub issue.
3. Fill in the form (only `filename` and `title` are required).
4. A maintainer will add the entry to `src/data/mods-metadata.json`.

You can also run the metadata gap report locally:

```bash
python3 scripts/check-mod-metadata.py
python3 scripts/check-mod-metadata.py --priority --output priority-mods.txt
```

To keep `src/data/mods-full-index.json` aligned with new metadata entries:

```bash
python3 scripts/sync-mod-metadata.py
```

**Accepted formats:** `.rbm` (ReBirth Mod File, v1.5+)

---

## Contributing Documentation

Historical documentation lives in `src/content/docs/` as Markdown files.
New files must include frontmatter matching the schema in `src/content/config.ts`:

```markdown
---
title: "ReBirth RB-338 — Your Doc Title"
version: "X.Y.Z"
releaseDate: "YYYY-MM-DD"
description: "One-sentence summary."
---
```

---

## Archive Indexers

The full `.rbs` song index (`src/data/songs-full-index.json`) is generated from the
remote archive rather than maintained by hand.

### Regenerate the song index

```bash
python3 scripts/index-rbs-archive.py
```

This recursively crawls `http://test.1ink.us/rb338/archive/rbs-songs`, extracts every
`.rbs` filename and size, infers collection/artist metadata from folder paths, and writes
`src/data/songs-full-index.json`. The script also computes coverage statistics against
`public/rbs-manifest.json`.

The generated index powers both the **Complete Song Index** on `/archive/songs` and the
on-site **folder detail pages** under `/archive/songs/<section>/<folder>/`.

Options:

```bash
python3 scripts/index-rbs-archive.py \
  --base-url http://test.1ink.us/rb338/archive/rbs-songs \
  --output src/data/songs-full-index.json \
  --manifest public/rbs-manifest.json
```

Run `npm run build` after regenerating the index so Astro picks up the new data.

---

## WebAssembly Audio Module

The `src/wasm/` directory is reserved for a future in-browser `.rbs` playback engine
built with WebAssembly. See [`src/wasm/README.md`](src/wasm/README.md) for the planned
architecture.

If you have C/C++/Rust audio DSP experience or knowledge of the `.rbs` binary format,
contributions to this module are especially welcome.

---

## Deployment

The production site is served from **GitHub Pages** at
`https://ford442.github.io/rb338` and rebuilds automatically from `main`.

For manual publishing there is `deploy.py`, which zips the `dist/` build and
uploads it as a single bundle to the storage manager.

> **Secrets never live in the repository.** All credentials are read from the
> environment. Copy `.env.example` to `.env` (which is git-ignored) and fill in
> your own values — do **not** paste secrets into any tracked file.

```bash
# 1. Configure credentials once (see .env.example for the full list)
cp .env.example .env
$EDITOR .env
set -a; source .env; set +a   # export the variables into your shell

# 2. Build, then deploy
npm run build
python3 deploy.py
```

| Variable            | Used by            | Purpose                                                  |
|---------------------|--------------------|----------------------------------------------------------|
| `DEPLOY_TOKEN`      | `deploy.py`        | Auth token for the storage endpoint (if required)        |
| `CONTABO_BASE_URL`  | `deploy.py`        | Override the storage endpoint URL                        |
| `DEPLOY_FOLDER`     | `deploy.py`        | Override the remote target folder                        |
| `SFTP_HOST` / `SFTP_USER` | SFTP tooling | Host + user for SFTP uploads (e.g. `scripts/rebirth_mod_upload.py`) |
| `SFTP_PASSWORD` *or* `SFTP_KEY_FILE` | SFTP tooling | Password **or** (preferred) private-key path |

`scripts/rebirth_mod_upload.py` accepts `--password` / `--key-file` on the
command line; prefer key-based auth and pass secrets from your environment
(e.g. `--password "$SFTP_PASSWORD"`) rather than typing them inline.

If any credential was ever committed to git history, **rotate it** — removing it
from the working tree does not remove it from history.

---

## Continuous Integration

GitHub Actions runs on every pull request and push to `main`. **No repository
secrets are required** for public CI — deploy credentials are only needed locally
for `deploy.py` and SFTP upload scripts.

| Workflow | Triggers | What it checks |
|----------|----------|----------------|
| [`ci.yml`](.github/workflows/ci.yml) | PR / `main` | `npm ci`, `astro check`, production build, secrets scan, Playwright against the preview server |
| [`wasm.yml`](.github/workflows/wasm.yml) | PRs touching `src/wasm/**`, nightly cron | Pinned Emscripten build, WASM Playwright tests, artifact upload |

Locally you can mirror the main CI pipeline:

```bash
npm ci
npm run ci          # check + build + playwright (dev server locally)
bash scripts/check-no-secrets.sh
python3 scripts/check-mod-metadata.py --priority   # optional warning
```

WASM builds require Emscripten on your machine (`npm run wasm:build`). See
[`src/wasm/README.md`](src/wasm/README.md) for setup.

---

## Code Style & Standards

- **Astro** components use `.astro` file extension
- **TypeScript** strict mode is enabled; avoid `any`
- **CSS** single source of truth: `public/styles/rebirth-theme.css` (linked in `BaseLayout.astro`). `src/styles/global.css` is a layering pointer only — do not import it.
- **Internal links** must use `normalizeBase(import.meta.env.BASE_URL)` from `src/lib/url.ts`
- **Linting**: `npm run lint` (ESLint flat config) and `npm run format` (Prettier) cover Astro/TS sources
- Keep the **retro-industrial aesthetic**: dark background, amber/green palette, monospace
  headings — but always prioritise accessibility (WCAG 2.1 AA)

---

## License

MIT — see [LICENSE](LICENSE) for details.  
ReBirth RB-338 software and assets are © Propellerhead Software AB (now Reason Studios).
This archive is a community project and is not affiliated with or endorsed by
Reason Studios.
