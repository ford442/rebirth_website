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
- [WebAssembly Audio Module](#webassembly-audio-module)
- [Code Style & Standards](#code-style--standards)
- [License](#license)

---

## Getting Started

### Prerequisites

- **Node.js** ≥ 18.17.1 — [download](https://nodejs.org/)
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
| `npm run dev`     | Start local dev server at `localhost:4321`        |
| `npm run build`   | Build the production site into `dist/`            |
| `npm run preview` | Preview the production build locally              |
| `npm run astro`   | Run Astro CLI directly (e.g. `npm run astro check`) |

---

## Project Structure

```
/
├── public/
│   └── archive/
│       ├── rbs-songs/   ← .rbs song files go here
│       └── rbm-mods/    ← .rbm mod files go here
├── src/
│   ├── components/
│   │   └── ModCard.astro        ← Reusable card component
│   ├── content/
│   │   ├── config.ts            ← Content collection schema
│   │   └── docs/                ← Historical docs (v1.0 → v2.0.1)
│   ├── layouts/
│   │   └── BaseLayout.astro     ← Shared HTML shell
│   ├── pages/
│   │   └── index.astro          ← Landing page
│   ├── styles/
│   │   └── global.css           ← Design tokens & base styles
│   └── wasm/
│       ├── audio-module.config.js  ← WASM config stub
│       └── README.md               ← WASM architecture notes
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
4. Add a corresponding entry to `src/pages/archive/songs.astro` (or open an issue if
   you'd prefer someone else to handle the metadata).
5. Open a **Pull Request** against the `main` branch with a brief description of the
   song, the approximate date it was created, and any software/hardware context.

**Accepted formats:** `.rbs` (ReBirth Song File, all versions)

### Contributing `.rbm` Mod Files

1. Place your `.rbm` file in `public/archive/rbm-mods/`
2. Use a descriptive filename: `tb303-silver-panel.rbm`
3. Include at minimum:
   - Mod name and version
   - Original author (if known)
   - Compatible ReBirth version(s)
4. Open a **Pull Request** with the above metadata in the PR description.

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

## WebAssembly Audio Module

The `src/wasm/` directory is reserved for a future in-browser `.rbs` playback engine
built with WebAssembly. See [`src/wasm/README.md`](src/wasm/README.md) for the planned
architecture.

If you have C/C++/Rust audio DSP experience or knowledge of the `.rbs` binary format,
contributions to this module are especially welcome.

---

## Code Style & Standards

- **Astro** components use `.astro` file extension
- **TypeScript** strict mode is enabled; avoid `any`
- **CSS** custom properties are defined in `src/styles/global.css`
- Keep the **retro-industrial aesthetic**: dark background, amber/green palette, monospace
  headings — but always prioritise accessibility (WCAG 2.1 AA)

---

## License

MIT — see [LICENSE](LICENSE) for details.  
ReBirth RB-338 software and assets are © Propellerhead Software AB (now Reason Studios).
This archive is a community project and is not affiliated with or endorsed by
Reason Studios.
