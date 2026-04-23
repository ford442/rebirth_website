# RBM Mods Archive Setup

## Overview
A searchable, filterable browser for ReBirth RB-338 RBM mod files (modifications and skins) hosted at `storage.1ink.us/rebirth_mods/`.

## Files Created

### 1. **Metadata File** (`src/data/mods-metadata.json`)
- Central repository for mod documentation
- 17 documented mods with metadata (title, author, year, description, tags)
- Easily extendable with more mods

```json
{
  "mods": [
    {
      "filename": "Aluwerk.rbm",
      "title": "Aluwerk",
      "author": "Dr Bruno & Syrusate",
      "description": "...",
      "tags": ["industrial", "metallic", "concept"]
    }
    // ... more mods
  ]
}
```

### 2. **Component** (`src/components/ModBrowserCard.astro`)
- Displays a single mod card with:
  - Title and year
  - Author information
  - Description
  - Tag badges (clickable for filtering)
  - Download button linking to `storage.1ink.us/rebirth_mods/`
- Styled with retro hardware theme (amber glow, dark panels)

### 3. **Page** (`src/pages/archive/mods.astro`)
- Full-featured mods browser at `/archive/mods`
- Features:
  - **Search**: Real-time search by title, author, description
  - **Tag filtering**: Quick filter buttons (All, Industrial, House, Dark, Synth, Official)
  - **Stats display**: Shows documented vs. total mods
  - **Responsive grid**: Adapts from multiple columns on desktop to single column on mobile
  - **Empty state**: Helpful message when no matches found
  - **Call-to-action**: Link to GitHub for contributing metadata

### 4. **Navigation**
- Mods archive link already exists in header navigation (BaseLayout.astro line 51)
- Accessible from: Home → Mods

## Usage

### Accessing the Page
- Live at: `/rebirth_website/archive/mods` (deployed)
- Local dev: `localhost:4321/rebirth_website/archive/mods`

### Adding More Mods
1. Add entry to `src/data/mods-metadata.json`:
```json
{
  "filename": "YourMod.rbm",
  "title": "Your Mod Title",
  "author": "Author Name",
  "year": 2000,
  "description": "Brief description",
  "tags": ["tag1", "tag2"]
}
```

2. Use any of these tags (or add new ones):
   - Genre: industrial, house, dark, synth, jazz, acid-jazz, electronic, experimental, tribal, world, funky
   - Type: official, default, classic, award-winning, toolbox, concept, campaign, etc.
   - Characteristics: metallic, sharp, clean, phat, minimalist, futuristic, etc.

3. Rebuild: `npm run build`

### Download URL
- Mods download from: `https://storage.1ink.us/rebirth_mods/{filename}`
- This URL is hardcoded in `ModBrowserCard.astro`

## Statistics

- **Documented mods**: 17 (with full metadata)
- **Available mods**: 600+ (files hosted at storage.1ink.us)
- **Tags**: 30+ (growing as more mods are documented)
- **Filter categories**: 6 quick filters on page

## Future Enhancements

- Batch import: Parse directory listing from `storage.1ink.us/rebirth_mods/` to auto-generate entries for undocumented mods
- Community contributions: Allow users to submit mod metadata via GitHub issues → convert to JSON entries
- Advanced filters: Sort by date, file size, author
- Playback preview: Integrate with future WebAssembly audio module (see `src/wasm/README.md`)

## Technical Details

- **Framework**: Astro SSG
- **Styling**: Pure CSS with design system tokens from `rebirth-theme.css`
- **Interactivity**: Client-side JavaScript for search/filter (no external dependencies)
- **Performance**: Zero external API calls; all data embedded at build time
- **Accessibility**: ARIA labels, semantic HTML, keyboard navigation
