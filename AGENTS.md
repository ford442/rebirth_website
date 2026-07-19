# ReBirth RB-338 Community Archive — Agent Guide

## Project Overview

This is a **static website archive** for the legendary **ReBirth RB-338** software synthesizer by Propellerhead Software (1997–2005, now abandonware). The site preserves and catalogs community-contributed `.rbs` song files, `.rbm` mod files, and historical documentation spanning versions 1.0 through 2.0.1.

- **Repository**: `ford442/rebirth_website`
- **Live URL**: `https://ford442.github.io/rb338`
- **Framework**: [Astro](https://astro.build) v6 (static site generator)
- **Language**: TypeScript with strict mode
- **Styling**: Pure CSS — no Tailwind, no UI frameworks
- **License**: MIT (ReBirth software itself is © Reason Studios / Propellerhead)

## Technology Stack

| Layer | Technology | Version |
|-------|-----------|---------|
| Framework | Astro | `^6.0.4` |
| Language | TypeScript | `^5.9.3` |
| Type checking | `@astrojs/check` | `^0.9.8` |
| Runtime | Node.js | latest (engine spec) |

**No testing framework** is currently configured.  
**No linter or formatter** is currently configured.  
Type safety is enforced solely by TypeScript strict mode (`astro/tsconfigs/strict` + `strictNullChecks`).

## Build & Development Commands

```bash
# Install dependencies
npm install

# Start dev server with hot reload (default: http://localhost:4321)
npm run dev

# Production build — outputs to dist/
npm run build

# Preview the production build locally
npm run preview

# Run Astro CLI directly (e.g. type checking)
npm run astro check
```

**Prerequisites**: Node.js ≥ 18.17.1, npm ≥ 9.

## Project Structure

```
├── public/
│   ├── archive/
│   │   ├── rbs-songs/          ← .rbs files (currently empty, .gitkeep only)
│   │   └── rbm-mods/           ← .rbm files (currently empty, .gitkeep only)
│   ├── styles/
│   │   └── rebirth-theme.css   ← Hardware-aesthetic CSS component library
│   └── rbs-manifest.json       ← Archive metadata index
├── src/
│   ├── pages/                  ← Astro routes (file-based routing)
│   │   ├── index.astro         ← Landing page (hero, featured songs/mods, about)
│   │   ├── rbs-archive.astro   ← Song archive browser (/rbs-archive)
│   │   └── archive/
│   │       ├── songs.astro     ← Song collection browser (/archive/songs)
│   │       ├── songs/
│   │       │   └── [...slug].astro ← On-site folder file listing (/archive/songs/<section>/<folder>)
│   │       └── mods.astro      ← Mod browser (/archive/mods)
│   ├── layouts/
│   │   └── BaseLayout.astro    ← Shared HTML shell (head, header, nav, footer)
│   ├── components/
│   │   ├── archive/
│   │   │   ├── ArchivePageShell.astro   ← Shared layout shell for /archive/songs and /archive/mods
│   │   │   ├── ArchiveEmptyState.astro  ← Reusable empty-state block for filtered results
│   │   │   └── ArchiveLink.astro        ← BASE_URL-aware internal archive link wrapper
│   │   ├── ui/
│   │   │   ├── HardwarePanel.astro      ← Rackmount/panel container with LED header
│   │   │   ├── Lcd.astro                ← LCD-style readout with label and value
│   │   │   ├── Led.astro                ← Status LED indicator
│   │   │   └── ActionButton.astro       ← Pattern (red) / Mix (green) hardware buttons
│   │   ├── Breadcrumbs.astro   ← Hardware-styled breadcrumb navigation
│   │   ├── ModCard.astro       ← Reusable card for songs/mods
│   │   ├── CollectionCard.astro← Card for archive folder sections
│   │   ├── ModBrowserCard.astro← Detailed mod card with download link
│   │   └── ExternalLink.astro  ← External archive link indicator
│   ├── content/
│   │   └── docs/               ← Historical markdown docs (v1.0, v1.5, v2.0, v2.0.1, etc.)
│   ├── data/
│   │   ├── mods-metadata.json  ← Documented mod metadata (26 of 367)
│   │   ├── mods-full-index.json← All 367 .rbm files with sizes
│   │   ├── songs-full-index.json← All catalogued .rbs files with metadata
│   │   ├── song-collections.ts ← Shared collection section definitions
│   │   └── archive-stats.ts    ← Single source of truth for archive stats
│   ├── styles/
│   │   └── global.css          ← Global reset, design tokens, base styles
│   ├── wasm/
│   │   ├── cpp/
│   │   │   ├── main.cpp                 # Emscripten entry point + embind exports
│   │   │   ├── build.sh                 # Emscripten compile script
│   │   │   ├── parser/                  # .rbs binary parser
│   │   │   ├── engine/                  # Audio engine (sequencer, mixer, voices)
│   │   │   ├── synth/                   # TB-303 / TR-808 / TR-909 voice stubs
│   │   │   └── worklet/                 # AudioWorklet processor callback
│   │   ├── js/
│   │   │   └── WasmAudioBridge.ts       # Typed JS wrapper around Emscripten Module
│   │   ├── types/
│   │   │   └── wasm-audio.ts            # Shared TypeScript interfaces
│   │   ├── tests/
│   │   │   └── wasm-audio-types.test.ts # Compile-time bridge contract test
│   │   ├── CONTRACT.md                  # C++ ↔ TypeScript field-for-field contract
│   │   ├── README.md                    # Planned WASM audio engine architecture
│   │   └── audio-module.config.js       # WASM runtime config stub
│   ├── content.config.ts       ← Astro content collection schema (docs)
│   └── env.d.ts                ← Astro client types reference
├── scripts/                    ← Python helper scripts
│   ├── download_peff_rbm_wayback.py  ← Download .rbm files from Wayback Machine
│   ├── rebirth_mod_upload.py   ← Download .rbm files and upload via SFTP
│   └── peff_rbm_filenames.txt  ← Filename list for wayback downloader
├── astro.config.mjs            ← Astro configuration
├── tsconfig.json               ← TypeScript strict config
├── deploy.py                   ← Manual bundle-upload deploy script (env-based creds)
└── package.json
```

### Routing

Astro uses file-based routing under `src/pages/`:

| File | Route |
|------|-------|
| File | Route |
|------|-------|
| `src/pages/index.astro` | `/` |
| `src/pages/rbs-archive.astro` | `/rbs-archive` |
| `src/pages/archive/songs.astro` | `/archive/songs` |
| `src/pages/archive/songs/[...slug].astro` | `/archive/songs/<section>/<folder>/...` |
| `src/pages/archive/mods.astro` | `/archive/mods` |
| `src/pages/history.astro` | `/history` |
| `src/pages/docs/index.astro` | `/docs` |
| `src/pages/docs/[...slug].astro` | `/docs/<slug>` |

## Design System & Styling

The site uses a **retro-industrial hardware aesthetic** modeled after the Roland TB-303 and TR-909 synthesizers.

### CSS Architecture

1. **`public/styles/rebirth-theme.css`** — Main consolidated theme file, linked in `BaseLayout.astro` `<head>`. Contains:
   - CSS custom properties (design tokens: colors, fonts, spacing, transitions)
   - CSS reset and base styles
   - Base typography
   - Layout utilities (`.container`, `.sr-only`)
   - Accessibility features (skip nav, focus-visible, scrollbars, reduced-motion support)
   - Hardware-themed component classes (`.rb-module`, `.rb-lcd`, `.rb-knob`, `.rb-step-btn`, `.rb-terminal`, etc.)
   - Button variants (`.rb-pattern-btn` = red, `.rb-mix-btn` = green)
   - LED indicators (`.rb-led--online`, `.rb-led--pending`, `.rb-led--offline`)
   - Transport controls, screws, chassis styling
   - Scanline and vignette effects

2. **Scoped `<style>` blocks** in individual `.astro` files for page-specific layout and component-specific styling.

### Key Theme Variables

```css
/* From rebirth-theme.css */
--rb-amber:        #ffb000;   /* Primary accent / LCD glow */
--rb-green:        #2d8a4e;   /* Success / pattern indicator */
--rb-red:          #c41e3a;   /* Danger / pattern button */
--rb-silver:       #c0c0c0;   /* Text, labels */
--rb-darkest:      #111111;   /* Background */
--rb-panel:        #262626;   /* Module background */
```

### Styling Rules

- **No external CSS frameworks** — pure CSS only.
- Use CSS Grid / Flexbox for layouts.
- Use `is:global` on `<style>` blocks only when injecting global stylesheets.
- All pages must use `BaseLayout.astro` for consistent shell, meta tags, and navigation.
- Archive browser pages (`/archive/songs`, `/archive/mods`) should use `ArchivePageShell.astro` so breadcrumbs, header panel, stats bar, section dividers, and empty states stay consistent.

## Code Style & Conventions

### Astro Components

- **Pages**: kebab-case filenames → `archive-songs.astro` routes to `/archive-songs`
- **Components**: PascalCase filenames → `ModCard.astro`
- All component props must be typed with `interface Props`
- Avoid `any` — strict mode is enabled

### TypeScript

```tsconfig.json
{
  "extends": "astro/tsconfigs/strict",
  "compilerOptions": {
    "strictNullChecks": true,
    "allowJs": true
  }
}
```

### Internal Links

Because the site is deployed under a **base path** (`/rb338`), **always** use `import.meta.env.BASE_URL` (or `normalizeBase(import.meta.env.BASE_URL)`) for internal links:

```astro
import { normalizeBase } from '../lib/url';
const base = normalizeBase(import.meta.env.BASE_URL);

<a href={`${base}/`}>Home</a>
<a href={`${base}/archive/songs/`}>Songs</a>
<a href={`${base}/archive/mods/`}>Mods</a>
```

The base path is configured in `astro.config.mjs`:

```js
export default defineConfig({
  site: 'https://ford442.github.io',
  base: '/rb338',
  compressHTML: true,
  build: { assets: '_assets' },
});
```

### Accessibility

- `BaseLayout.astro` provides:
  - Skip-to-content link (`.skip-nav`)
  - Semantic HTML (`<header>`, `<nav>`, `<main>`, `<footer>`)
  - Canonical URL and Open Graph meta tags
  - WCAG 2.1 AA contrast compliance (verified for amber `#ffb000` on dark backgrounds)
- All interactive elements must have `aria-label` where text content is insufficient.
- Decorative hardware elements use `aria-hidden="true"`.

## Content Collections

Historical documentation lives in `src/content/docs/` as Markdown files with frontmatter.

### Schema (`src/content.config.ts`)

```ts
import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';

const docs = defineCollection({
  loader: glob({ pattern: '**/*.md', base: './src/content/docs' }),
  schema: z.object({
    title: z.string(),
    version: z.string(),
    releaseDate: z.string().optional(),
    description: z.string().optional(),
  }),
});

export const collections = { docs };
```

### Required Frontmatter

```markdown
---
title: "ReBirth RB-338 — Your Doc Title"
version: "X.Y.Z"
releaseDate: "YYYY-MM-DD"
description: "One-sentence summary."
---
```

**Note**: Only `title` and `version` are required. `releaseDate` and `description` are optional.

Docs are rendered by `src/pages/docs/index.astro` (listing) and `src/pages/docs/[...slug].astro` (individual pages).

## Data Files

### `src/data/mods-metadata.json`

Structured metadata for documented `.rbm` mod files:

```json
{
  "mods": [
    {
      "filename": "Metallicon.rbm",
      "title": "Metallicon",
      "author": "Propellerhead",
      "year": 1998,
      "description": "One of the four official default mods...",
      "tags": ["official", "default", "classic", "metal"]
    }
  ]
}
```

- `filename` must match the file hosted at `https://storage.1ink.us/rebirth_mods/{filename}`
- `year` is nullable
- `tags` is an array of lowercase kebab-case strings

Currently **26 mods** are documented out of **367** available (17 with full metadata, 9 with minimal metadata).

### `public/rbs-manifest.json`

Static metadata index for the RBS song archive. Describes collections, artists, and statistics. Used by the archive browser page.

## External Assets

The actual binary files are **not stored in this repository** (kept in `.gitkeep`-only directories). They are hosted externally:

| Asset Type | Host | URL Pattern |
|-----------|------|-------------|
| `.rbs` songs | `test.1ink.us` | `http://test.1ink.us/rb338/archive/rbs-songs/{collection}/{folder}` |
| `.rbm` mods | `storage.1ink.us` | `https://storage.1ink.us/rebirth_mods/{filename}` |

The site acts as a catalog/browser that links out to these external hosts.

## Python Scripts

### `scripts/download_peff_rbm_wayback.py`

Downloads `.rbm` files from a Wayback Machine snapshot of peff.com. Uses `peff_rbm_filenames.txt` as input.

```bash
python scripts/download_peff_rbm_wayback.py --output-dir ./peff_rbm_downloads
```

### `scripts/rebirth_mod_upload.py`

Downloads selected `.rbm` files from `cdn.peff.com` and uploads them to an SFTP server.

```bash
python scripts/rebirth_mod_upload.py \
  --host storage.1ink.us \
  --user $USERNAME \
  --password $PASSWORD \
  --remote-base /path/to/rebirth_mods
```

### `scripts/index-rbs-archive.py`

Recursively crawls the remote RBS song archive at `test.1ink.us` and generates
`src/data/songs-full-index.json` with direct download URLs, file sizes, and inferred
collection/artist metadata. Run this whenever files are added to the remote archive.

```bash
python3 scripts/index-rbs-archive.py
```

Options:

```bash
python3 scripts/index-rbs-archive.py \
  --base-url http://test.1ink.us/rb338/archive/rbs-songs \
  --output src/data/songs-full-index.json \
  --manifest public/rbs-manifest.json
```

### `scripts/check-mod-metadata.py`

Diffs `src/data/mods-full-index.json` against `src/data/mods-metadata.json` and
reports undocumented `.rbm` files. Use `--priority` to surface likely high-value
mods first, and `--output` to save a batch contribution list. Exits with code 1
when gaps exist, so it can be used as a CI warning.

```bash
python3 scripts/check-mod-metadata.py
python3 scripts/check-mod-metadata.py --priority --output priority-mods.txt
```

### `scripts/sync-mod-metadata.py`

Aligns `src/data/mods-full-index.json` with `src/data/mods-metadata.json`,
setting `hasMeta`, `title`, `author`, and `tags` for documented mods. Run this
after updating metadata so the mod browser badges and coverage bar stay in sync.

```bash
python3 scripts/sync-mod-metadata.py
```

### `deploy.py`

Manual deployment script that zips the `dist/` build and uploads it as a single bundle to the storage manager (`CONTABO_BASE_URL`, default `https://storage.noahcohn.com`).

**Credentials come from the environment — never hardcode secrets.** Set `DEPLOY_TOKEN` (and optionally `CONTABO_BASE_URL` / `DEPLOY_FOLDER`) via your shell or a git-ignored `.env` file. See `.env.example` and the [Deployment](README.md#deployment) section of the README. The legacy `deploy_old.py` (direct SFTP with an inline password) has been removed.

## WebAssembly Audio Module (Future)

The `src/wasm/` directory is reserved for a planned in-browser `.rbs` playback engine.

- **Status**: `PENDING` — no compiled binaries exist
- **Planned toolchain**: Emscripten (C/C++) or wasm-pack (Rust)
- **Architecture**: `.rbs` binary → WASM parser → AudioWorkletProcessor → Web Audio API
- **Config stub**: `src/wasm/audio-module.config.js` defines runtime parameters (sample rate, buffer size, feature flags)

Contributions from anyone with audio DSP or `.rbs` binary format knowledge are welcome.

## Testing Strategy

**No automated tests are currently configured.**

For manual verification:

1. `npm run dev` — verify all routes load without Astro errors
2. `npm run build` — verify the build succeeds with zero TypeScript errors
3. `npm run preview` — verify the production build renders correctly
4. Check that all internal links use `import.meta.env.BASE_URL`
5. Verify contrast ratios for new UI elements against the dark theme

## Deployment

### GitHub Pages (primary)

- Auto-deploys from the `main` branch
- Site: `https://ford442.github.io/rb338`
- Base path `/rb338` is baked into `astro.config.mjs`

### Manual bundle upload (secondary)

- `deploy.py` zips `dist/` and uploads it to the storage manager (`CONTABO_BASE_URL`).
- Requires `requests`. Credentials (`DEPLOY_TOKEN`, etc.) are read from the environment / `.env` — never hardcoded.

## Security Considerations

- **No secrets in tracked files.** Deploy/upload credentials are supplied via environment variables or a git-ignored `.env` (see `.env.example`). The legacy `deploy_old.py` (inline SFTP password) has been removed. Any credential ever committed to history should be rotated.
- No user authentication, sessions, or backend API — this is a fully static site.
- External download links open in new tabs with `rel="noopener noreferrer"`.
- No `target="_blank"` on internal links.

## Known Gaps & TODOs

1. **External link clarity**: Archive downloads and folder browse links now use the `ExternalLink` component with host labels and `(opens in new tab)` cues.
2. **Incomplete mod metadata**: 26 of 367 mods are documented in `mods-metadata.json`. A GitHub issue template (`.github/ISSUE_TEMPLATE/mod-metadata.yml`) and helper scripts (`check-mod-metadata.py`, `sync-mod-metadata.py`) now exist to close this gap.
3. **Empty archive directories**: `public/archive/rbs-songs/` and `public/archive/rbm-mods/` contain only `.gitkeep` files; actual assets are hosted externally.
4. **WASM module**: Not yet implemented — purely architectural stubs.
5. **No tests**: No unit, integration, or E2E tests exist.
6. **No linting**: No ESLint, Prettier, or Stylelint configuration.

## Contributing Files

### Adding `.rbs` Song Files

1. Fork and branch: `git checkout -b add/my-song-name`
2. Place `.rbs` in `public/archive/rbs-songs/`
3. Use lowercase, hyphen-separated filenames: `acid-bassline-140bpm.rbs`
4. Update `public/rbs-manifest.json` and/or `src/pages/rbs-archive.astro`
5. Open PR against `main`

### Adding `.rbm` Mod Files

1. Place `.rbm` in `public/archive/rbm-mods/`
2. Add metadata entry to `src/data/mods-metadata.json`
3. Open PR with mod name, version, author, and compatible ReBirth version(s)

### Adding Documentation

1. Create Markdown file in `src/content/docs/`
2. Include required frontmatter (see Content Collections section)
3. **Also create or update the docs rendering page** so the new doc is visible

## Cursor Cloud specific instructions

Single service: an Astro static site. Standard commands live in `package.json` (`dev`, `build`, `preview`, `astro`) and `playwright.config.ts` (`test`). The startup update script already runs `npm install` and `npx playwright install`.

- Dev server: `npm run dev` serves under the base path — open `http://localhost:4321/rb338/`, NOT `http://localhost:4321/`. The bare root returns 404 because `base: '/rb338'` is set in `astro.config.mjs`.
- Playwright `webServer` auto-starts `npm run dev` and reuses an already-running dev server locally, so tests do not need a separate server.
- Test suite state (as of setup): most tests in `tests/` are pre-existing failures unrelated to environment. They assert on an outdated `.rb-panel` class (the markup now uses `rb-rack` / `rb-panel__*` / `rb-panel--*`), and `tests/rbs-player.spec.ts` targets the wrong port (`localhost:3000` instead of `4321`). Do not treat these as environment breakage.
- WASM tests (`tests/wasm-*.spec.ts`) require built artifacts under `public/wasm/` (e.g. `rbsParser.js/.wasm`, `rbsWorklet.js`). These are NOT built — the WASM audio module is a `PENDING` stub and building it needs the Emscripten toolchain (`emcc`, not installed). Expect these to fail until the module is built.
- Type safety is the primary check: `npm run astro check` passes with 0 errors (only lint-style hints). There is no ESLint/Prettier.
