# Copilot Instructions for ReBirth RB-338 Archive

## Quick Reference

### Build, Test & Lint

```bash
# Development server (auto-reload at localhost:4321)
npm run dev

# Production build
npm run build

# Preview production build locally
npm run preview

# Run Astro CLI directly (e.g., type checking)
npm run astro check
```

No linting or testing tools are currently configured. Rely on TypeScript strict mode for type safety.

## Project Architecture

### What This Is

A static site archive for the legendary **ReBirth RB-338** software synthesizer (Propellerhead Software, 1997–2.0.1). The site preserves community-contributed songs (.rbs files), mods (.rbm files), and historical documentation.

### High-Level Structure

```
src/
├── pages/              # Astro routes (auto-generated from .astro files)
│   ├── index.astro     # Landing page with featured songs/mods
│   └── rbs-archive.astro  # Archive page showing collection folders
├── layouts/
│   └── BaseLayout.astro    # HTML shell with header, nav, footer
├── components/
│   ├── ModCard.astro       # Reusable card for songs/mods (with featured badges)
│   └── CollectionCard.astro # Card for collection sections (songs, mods)
├── content/
│   └── docs/               # Historical markdown docs with frontmatter
├── styles/
│   └── global.css          # Design tokens & base styles
└── wasm/
    └── README.md           # Planned architecture for browser .rbs playback

public/
├── archive/
│   ├── rbs-songs/          # User-contributed .rbs files
│   ├── rbm-mods/           # User-contributed .rbm files
│   └── rbs-manifest.json   # (Optional) metadata index
└── styles/
    └── rebirth-theme.css   # Hardware aesthetic theme (TB-303/TR-909)
```

### Design System

The site uses a **retro-industrial hardware aesthetic** modeled after TB-303 and TR-909 synthesizers. All styling is CSS-based with CSS custom properties for theming.

**Key theme CSS variables** (defined in `public/styles/rebirth-theme.css`):
- `--rb-amber`: Primary accent color (#ffb000, LCD glow)
- `--rb-red`: Danger/pattern button (#c41e3a)
- `--rb-green`: Success/pattern indicator (#3dba66)
- `--rb-silver`: Text, labels (#c0c0c0)
- `--rb-darkest`: Black background (#111111)
- `--rb-panel`: Module background (#262626)

**Base theme variables** (defined in `src/styles/global.css`):
- `--color-amber`, `--color-green`: Primary/secondary accents
- `--font-mono`: Monospace for headings/displays
- `--font-sans`: System font for body
- `--space-xs` through `--space-xl`: Spacing scale

**To apply the theme to a new page:**
1. Use `BaseLayout.astro` as the wrapper (it imports global styles and links the theme)
2. Use class names: `rb-header`, `rb-nav`, `rb-logo`, `rb-panel`, `rb-module`, `rb-button`, `rb-knob`, etc.
3. Reference CSS variables for consistent colors and spacing

### Content Collections

Documents (historical ReBirth manuals, release notes, etc.) are stored in `src/content/docs/` using Astro's content collection loader. Each file requires this frontmatter:

```markdown
---
title: "ReBirth RB-338 — Your Doc Title"
version: "X.Y.Z"
releaseDate: "YYYY-MM-DD"
description: "One-sentence summary."
---
```

Schema is defined in `src/content.config.ts`. Only `title` and `version` are required; `releaseDate` and `description` are optional.

## Key Conventions

### Naming & File Organization

- **Pages**: `.astro` files in `src/pages/` automatically become routes. Use kebab-case: `archive-songs.astro` → `/archive-songs`
- **Components**: Reusable `.astro` files in `src/components/`. PascalCase names: `ModCard.astro`
- **Archive files**: Lowercase, hyphen-separated: `acid-bassline-140bpm.rbs`, `tb303-silver-panel.rbm`

### TypeScript & Strict Mode

- **Strict mode enabled** in `tsconfig.json` (`astro/tsconfigs/strict`)
- Use `z` schema validation from `astro:content` for content collections (avoid `any` types)
- All component props must be typed with `interface Props`

### Accessibility & SEO

- All pages use `BaseLayout.astro` which provides:
  - Meta tags (canonical, OG tags, description)
  - Skip-to-content link for keyboard navigation
  - Semantic HTML (`<header>`, `<nav>`, `<main>`, `<footer>`)
  - WCAG 2.1 AA contrast compliance (amber #ffb000 vs dark backgrounds)

### URL Base Path

All internal links must use `${import.meta.env.BASE_URL}` because the site is deployed to `/rebirth_website`:

```astro
<a href={`${import.meta.env.BASE_URL}`}>Home</a>
<a href={`${import.meta.env.BASE_URL}archive/songs`}>Songs</a>
```

### CSS & Styling

- **Global styles**: `src/styles/global.css` (imported in `BaseLayout.astro`)
- **Theme**: `public/styles/rebirth-theme.css` (linked in BaseLayout `<head>`)
- **Component styles**: Scoped `<style>` blocks in `.astro` files (use `is:global` for global sheets)
- **No external frameworks**: Pure CSS. Use CSS Grid/Flexbox for layouts.

## Deployment & Site Configuration

- **Site**: Deployed to GitHub Pages at `https://ford442.github.io/rebirth_website`
- **Base path**: `/rebirth_website` (configured in `astro.config.mjs`)
- **Build output**: `dist/` directory (ignored in git)
- **Manual deployment**: `deploy.py` script uses SFTP to upload `dist/` to `test.1ink.us/rb338`

## Contributing Workflow

Contributors are expected to:
1. Add `.rbs` or `.rbm` files to `public/archive/rbs-songs/` or `public/archive/rbm-mods/`
2. Add metadata/cards to the relevant page (e.g., update `index.astro` featured songs/mods)
3. For doc contributions, add Markdown files to `src/content/docs/` with required frontmatter
4. Open PR against `main` branch

See README.md for full contribution guidelines.

## WebAssembly Audio Module (Future)

The `src/wasm/` directory is reserved for an in-browser `.rbs` playback engine. This is not yet implemented but is planned. See `src/wasm/README.md` for architecture notes.

If you have C/C++/Rust audio DSP experience or knowledge of the `.rbs` binary format, contributions are welcome.
