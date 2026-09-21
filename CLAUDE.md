# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Commands

```bash
npm run dev      # Dev server at localhost:4321 (auto-reloads)
npm run build    # Production build → dist/
npm run preview  # Preview production build locally
npm run astro    # Run Astro CLI directly (e.g., npm run astro check)
```

```bash
npm run check          # astro check (TypeScript strict mode — the primary gate)
npm run lint           # ESLint
npm run format         # Prettier --check
npm run contract:check # C++ ↔ TypeScript WASM contract
npm run wasm:test      # Native C++ doctest suite
npm test               # Playwright browser/E2E specs
npm run ci             # check → contract:check → build → test
```

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
└── wasm/                     # In-browser .rbs playback engine (C++ compiled to WASM)
    ├── CONTRACT.md           # C++ ↔ TypeScript field-for-field contract (SSOT)
    ├── README.md             # Architecture, build, status (PARTIAL ENGINE, SHIPPING PIPELINE)
    ├── cpp/                  # Parser, sequencer, mixer, TB-303/TR-808/TR-909 voices
    ├── js/                   # WasmAudioBridge, bounce client, player UI (dom/transport/studio-view split)
    └── types/                # wasm-audio-{song,engine,config,mod}.ts + WASM→UI mapping

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
title: 'ReBirth RB-338 — Your Doc Title'
version: 'X.Y.Z'
releaseDate: 'YYYY-MM-DD' # Optional
description: 'One-sentence summary.' # Optional
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

## WebAssembly Audio Module

`src/wasm/` is a **shipping** in-browser `.rbs` playback engine (C++ compiled to
WASM via Emscripten, CI-built and deployed). It parses v1/v1.5 and v2 `.rbs`,
sequences the arrangement, synthesises TB-303 (ZDF ladder filter + PolyBLEP
oscillators) and TR-808/TR-909 — procedurally or from `.rbm` mod samples —
bounces offline to WAV (full mix or per-device stems), and exports `.mid` from
TypeScript. Pattern steps are editable in the studio grid through
`RbsAudioEngine::setStep` (one patch per click, session-only). It does **not**
write `.rbs` or render `.rbm` skins, and TRAK automation plays only through the
`loadSongFromBytes` path. See
`src/wasm/README.md` for the full status and roadmap.

### Which language owns what

| Language       | Owns                                                                                      | Must not do                                                   |
| -------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| **C++**        | `.rbs`/`.rbm` parsing, DSP, sequencer, mixer, sample decode, WAV bytes, RT-safe snapshots | Skins, DOM, `fetch`, MIDI SMF writing (already TypeScript)    |
| **TypeScript** | AudioContext, the Embind boundary, player UI, MiniSearch, SMF writer, download helpers    | Inner-loop DSP, owning PCM, growing WASM memory               |
| **Astro**      | Markup, `BASE_URL` links, content collections                                             | Engine logic — `RbsPlayer.astro`'s `<script>` only bootstraps |

New files land in the layer that owns the job; if a change does not fit the
table, the split is wrong.

### Key points for anyone touching this code

- **`src/wasm/CONTRACT.md` is the single source of truth** for the C++ ↔ TypeScript data contract (`main.cpp` Embind registrations ↔ `src/wasm/types/wasm-audio-*.ts`). Run `npm run contract:check` after changing either side — it fails CI on drift (struct fields, `DeviceParamId` values).
- The shared TS types are split one job per file: `wasm-audio-song.ts` (ParsedSong / devices / patterns), `wasm-audio-engine.ts` (the Embind surface and offline bounce), `wasm-audio-config.ts` (runtime module config), `wasm-audio-mod.ts` (`.rbm` enums and reports). There is no barrel — import from the file that owns the type.
- Live device/mixer parameters flow through `RbsAudioEngine::setDeviceParam(deviceId, paramId, value)`, where `paramId` is the numeric `DeviceParamId` enum (`src/wasm/cpp/engine/EngineCommands.h`) — not a string. `Voice::setParameter(DeviceParamId, float)` has zero string comparisons on the audio thread.
- The C++ parser is split by seam: `RbsParser.cpp` (container + chunk decoding), `RbsTrak.cpp` (TRAK/STRAK events + arrangement), `RbsMidiContainer.cpp` (v1/v1.5), with `RbsByteStream.h` holding the shared bounds-checked reader. `sources.cmake` is the only source list — never add `.cpp` files to `CMakeLists.txt`, `Makefile`, or `build.sh`.
- The player UI is split into `src/wasm/js/player-dom.ts` (DOM lookup), `player-transport.ts` (status/toasts/play-stop/volume/tempo), `player-studio-view.ts` (pattern grid + device knobs), composed by `player-ui.ts`. Offline bounce lives in `wasm-bounce.ts` (main thread) + `bounce-worker.ts` (second WASM instance).
- Run `npm run wasm:native:configure` once after cloning so clangd can resolve `parser/` vs `native_stubs/` headers from `src/wasm/cpp/build/compile_commands.json`.
- `npm run wasm:test` runs the native (non-Emscripten) C++ unit tests; `npm run wasm:build` builds the actual WASM binaries (requires Emscripten).

Contributions from audio DSP or `.rbs`/`.rbm` binary format experts welcome — see `src/wasm/README.md`'s roadmap for what's left.
