# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Commands

```bash
npm run dev      # Dev server at localhost:4321 (auto-reloads)
npm run build    # Production build → dist/
npm run preview  # Preview production build locally
npm run astro    # Run Astro CLI directly (e.g., npm run astro check)
```

**No linting or testing tools configured.** TypeScript strict mode (`tsconfig.json: astro/tsconfigs/strict`) is the primary code safety mechanism.

## Architecture Overview

This is a **static archive site** for the ReBirth RB-338 synthesizer built with **Astro 6** and TypeScript. The site preserves community-contributed songs (`.rbs` files), mods (`.rbm` files), and historical documentation.

### File Organization

```
src/
├── pages/            # Auto-routes to URLs (kebab-case filenames)
│   ├── index.astro           → /
│   ├── rbs-archive.astro     → /rbs-archive
│   └── archive/mods.astro    → /archive/mods
├── layouts/
│   └── BaseLayout.astro      # Shared HTML shell, header, nav, footer
├── components/       # Reusable Astro components (PascalCase)
│   ├── ModCard.astro
│   ├── ModBrowserCard.astro
│   └── CollectionCard.astro
├── content/
│   ├── config.ts             # Content collection schema (docs)
│   └── docs/                 # Historical markdown documentation
├── styles/
│   └── (legacy, consolidated into public/styles/rebirth-theme.css)
└── wasm/
    └── README.md             # Planned browser playback engine (not implemented)

public/
├── archive/
│   ├── rbs-songs/            # User-contributed .rbs files
│   ├── rbm-mods/             # User-contributed .rbm files
│   └── rbs-manifest.json     # (Optional) metadata
└── styles/
    └── rebirth-theme.css     # Hardware aesthetic theme (TB-303/TR-909)
```

## Key Conventions & Patterns

### Pages & Routes

- `.astro` files in `src/pages/` automatically become routes
- Use kebab-case filenames: `archive-songs.astro` → `/archive-songs`
- All pages should use `BaseLayout.astro` as the wrapper

### URL Base Path (Critical)

The site is deployed to `/rebirth_website`, so **all internal links must include the base path**:

```astro
<a href={`${import.meta.env.BASE_URL}`}>Home</a>
<a href={`${import.meta.env.BASE_URL}archive/songs`}>Songs</a>
```

Without this, links will break on the deployed site.

### Content Collections

Historical documentation lives in `src/content/docs/` as Markdown files. Each file **requires** this frontmatter:

```markdown
---
title: "ReBirth RB-338 — Your Doc Title"
version: "X.Y.Z"
releaseDate: "YYYY-MM-DD"     # Optional
description: "One-sentence summary."  # Optional
---
```

Schema is defined in `src/content.config.ts`. The loader uses `glob` to auto-discover `.md` files.

### Design System

The site uses a **retro-industrial hardware aesthetic** modeled after TB-303 and TR-909 synthesizers.

**CSS custom properties** (theme and base variables):
- `--rb-amber`, `--rb-red`, `--rb-green`, `--rb-silver`, `--rb-darkest`, `--rb-panel` (hardware colors)
- `--color-amber`, `--color-green` (semantic accents)
- `--font-mono`, `--font-sans` (typography)
- `--space-xs` through `--space-xl` (spacing scale)

Apply theme via semantic class names: `rb-header`, `rb-nav`, `rb-logo`, `rb-panel`, `rb-module`, `rb-button`, `rb-knob`, etc.

**Styling approach**: Consolidated theme in `public/styles/rebirth-theme.css` (linked in BaseLayout `<head>`). Contains all global styles including design tokens, base styles, resets, typography, and accessibility features. Component styles use scoped `<style>` blocks in `.astro` files. Pure CSS; no frameworks.

### TypeScript & Component Props

- Strict mode enabled; avoid `any` types
- Component props must be typed with `interface Props`
- Use Zod schema validation from `astro:content` for content collections

### Deployment

- **GitHub Pages**: `https://ford442.github.io/rebirth_website`
- **Base path**: `/rebirth_website` (in `astro.config.mjs`)
- **Build output**: `dist/` directory (git-ignored)

## Contributing Workflow

Contributors add:
1. `.rbs` files to `public/archive/rbs-songs/` (with kebab-case names)
2. `.rbm` files to `public/archive/rbm-mods/`
3. Metadata/card entries to the relevant page
4. Markdown docs to `src/content/docs/` with required frontmatter

Full guidelines in `README.md`.

## Future: WebAssembly Audio Module

`src/wasm/` is reserved for an in-browser `.rbs` playback engine (not yet implemented). See `src/wasm/README.md` for planned architecture. Contributions from audio DSP or `.rbs` binary format experts welcome.
