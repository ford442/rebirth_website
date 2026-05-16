# ReBirth RB-338 Design System

> Living reference for the retro-industrial hardware aesthetic used across the archive site.
> Last updated: 2026-05-16

---

## Philosophy

The design language models the physical Roland TB-303 / TR-909 synthesizer hardware:

- **Dark gunmetal panels** with subtle gradient sheen
- **Amber and green LEDs** as primary accents (not arbitrary brand colors)
- **Courier New / Share Tech Mono** for all hardware-facing typography
- **Physical affordances**: screws, bevels, inset shadows, raised borders
- **90s computing aesthetic**: scanlines, terminal readouts, phosphor glow

---

## 1. Color Tokens

### Semantic Palette

| Token | Hex | Usage |
|-------|-----|-------|
| `--rb-amber` | `#ffb000` | Primary accent, LCD text, active states, links |
| `--rb-amber-dim` | `#b37800` | Amber borders, inactive amber elements |
| `--rb-amber-glow` | `rgba(255,176,0,0.4)` | Box-shadow glows on hover/active |
| `--rb-green` | `#39ff14` | Success, online status, song indicators |
| `--rb-green-bright` | `#3dba66` | Hover green, brighter accent |
| `--rb-green-dim` | `#1a5230` | Green borders, inactive green elements |
| `--rb-green-glow` | `rgba(57,255,20,0.4)` | Green glow shadows |
| `--rb-red` | `#c41e3a` | Danger, pattern buttons, offline indicator |
| `--rb-red-dim` | `#8a1528` | Red borders, darker red states |
| `--rb-red-glow` | `rgba(196,30,58,0.5)` | Red glow shadows |
| `--rb-silver` | `#c0c0c0` | Primary body text |
| `--rb-silver-dim` | `#888888` | Secondary text, labels, muted content |

### Structural Palette

| Token | Hex | Usage |
|-------|-----|-------|
| `--rb-darkest` | `#111111` | Page background |
| `--rb-darker` | `#1e1e1e` | Panel interiors, terminal backgrounds |
| `--rb-dark` | `#2d2d2d` | Elevated surfaces |
| `--rb-panel` | `#262626` | Standard panel background |
| `--rb-panel-raised` | `#303030` | Slightly raised surfaces |
| `--rb-border` | `#3a3a3a` | Default borders |
| `--rb-border-hi` | `#555555` | Highlighted/top borders |

### LCD Palette

| Token | Hex | Usage |
|-------|-----|-------|
| `--rb-lcd-bg` | `#1a1a0a` | LCD display background |
| `--rb-lcd-text` | `#ffb000` | LCD numeric/text values |
| `--rb-lcd-dim` | `#7a5500` | LCD label text above values |

### Phosphor (Terminal)

| Token | Hex | Usage |
|-------|-----|-------|
| `--rb-phosphor` | `#33ff66` | Terminal green text |
| `--rb-phosphor-dim` | `#1a7a33` | Dimmed terminal text |

### Contrast Compliance

All amber (`#ffb000`) text on dark backgrounds (`#111`–`#262626`) meets **WCAG 2.1 AA** contrast ratios. Silver (`#c0c0c0`) on dark also passes. Dim silver (`#888`) is used for non-essential labels only.

---

## 2. Typography

### Font Stack

```css
--rb-font-display: 'Share Tech Mono', 'Courier New', monospace;
--rb-font-label: system-ui, -apple-system, sans-serif;
```

**Rule**: All hardware-facing UI (buttons, labels, LCDs, LEDs, headers) uses `var(--rb-font-display)`. Body paragraphs may use `var(--rb-font-label)` for readability.

### Type Scale

| Element | Size | Weight | Letter-Spacing | Transform |
|---------|------|--------|----------------|-----------|
| Page title (H1) | `clamp(1.6rem, 6vw, 2.8rem)` | 700 | 0.08em | — |
| Panel header title | `0.75rem` | normal | 0.2em | uppercase |
| Section heading | `1.8rem` | bold | 0.06em | uppercase |
| LCD value | `1.4rem` / `1rem` (sm) | bold | — | — |
| LCD label | `0.6rem` | normal | 0.2em | uppercase |
| LED label | `0.65rem` | normal | 0.1em | uppercase |
| Button text | `0.8rem` | normal | 0.1em | uppercase |
| Nav button | `0.75rem` | normal | 0.12em | uppercase |
| Card title | `0.95rem` | normal | 0.03em | — |
| Body text | `1rem` (16px base) | normal | — | — |
| Caption / meta | `0.7rem`–`0.8rem` | normal | 0.05em–0.1em | uppercase |

### Text Effects

- **Amber glow**: `text-shadow: 0 0 12px var(--rb-amber-glow), 0 0 24px rgba(255,176,0,0.2);`
- **LCD glow**: `text-shadow: 0 0 8px var(--rb-amber-glow), 0 0 16px rgba(255,176,0,0.2);`
- **Master title glow**: Large amber text gets multi-layer glow for neon effect.

---

## 3. Component Patterns

### Panel (`HardwarePanel`)

The foundational layout unit. Every major content block lives in a panel.

```astro
<HardwarePanel
  variant="default"   /* default | amber | green | red | chassis */
  animate={true}      /* entrance animation */
  screwCross={false}  /* Phillips-head screws in corners */
  title="PANEL TITLE"
  ledStatus="online"
  ledLabel="OK"
>
  <!-- body content -->
</HardwarePanel>
```

**CSS anatomy**:

```css
.rb-panel {
  background: linear-gradient(180deg, #2e2e2e 0%, #252525 100%);
  border: 1px solid var(--rb-border);
  border-top: 2px solid var(--rb-border-hi);
  border-radius: 8px;
  box-shadow: 0 4px 24px rgba(0,0,0,0.6), inset 0 1px 0 rgba(255,255,255,0.04);
  overflow: hidden;
  position: relative;
  margin-bottom: 1.5rem;
}
```

**Variants**:
- `default` — neutral gray panel
- `amber` — left border accent in amber (`border-left: 3px solid var(--rb-amber-dim)`)
- `green` — left border accent in green
- `red` — left border accent in red
- `chassis` — darker, heavier border (`#111`, `2px`), more inset shadow; used for master header

**Header** (`.rb-panel__header`):
```css
background: linear-gradient(180deg, #333 0%, #2a2a2a 100%);
border-bottom: 1px solid #111;
padding: 0.6em 1em;
display: flex;
align-items: center;
justify-content: space-between;
gap: 0.5em;
```

**Body** (`.rb-panel__body`): `padding: 1.25rem`

**Screws** (`ScrewCorners`): Four absolutely positioned pseudo-elements using `conic-gradient` metallic sheen. Cross variant adds intersecting lines.

---

### LED (`Led`)

Status indicator with pulsing animation.

```astro
<Led status="online" label="ARCHIVE ONLINE" />
```

**States**:

| Status | Color | Animation |
|--------|-------|-----------|
| `online` | `#39ff14` (green) | `rb-pulse-green` — 2s ease-in-out infinite |
| `pending` | `#ffb000` (amber) | `rb-pulse-amber` — 2s ease-in-out infinite |
| `offline` | `#8a1528` (red dim) | `rb-pulse-red-slow` — 3s ease-in-out infinite |
| `flicker` | `#ffb000` | `rb-flicker-amber` — 1.5s steps(8) infinite |
| `flicker-green` | `#39ff14` | `rb-flicker-green` — 1.5s steps(8) infinite |

**Structure**: Inline flex with 8px dot + label text. Dot uses `border-radius: 50%` with layered `box-shadow` for glow.

---

### LCD (`Lcd`)

Numeric readout display.

```astro
<Lcd label="SONGS" value="2000" unit="+" size="default" />
```

**CSS**:
```css
.rb-lcd {
  background: var(--rb-lcd-bg);
  color: var(--rb-lcd-text);
  border: 2px solid #111;
  border-radius: 4px;
  padding: 0.6em 1em;
  box-shadow: inset 0 0 12px rgba(0,0,0,0.8), inset 0 0 4px rgba(255,176,0,0.08);
}
```

Includes a scanline overlay pseudo-element (`repeating-linear-gradient`) at 4px intervals for authentic LCD texture.

---

### Knob (`Knob`)

Decorative rotary control.

```astro
<Knob label="CUT OFF" rotation={30} />
```

**CSS**:
```css
.rb-knob__dial {
  width: 40px;   /* 32px on mobile */
  height: 40px;
  border-radius: 50%;
  background: conic-gradient(from 180deg, #333 0%, #555 25%, #777 50%, #555 75%, #333 100%);
  box-shadow: 0 2px 6px rgba(0,0,0,0.6), inset 0 1px 3px rgba(255,255,255,0.12);
  transition: transform 0.3s cubic-bezier(0.22, 1, 0.36, 1);
}

.rb-knob:hover .rb-knob__dial {
  transform: rotate(15deg);
}
```

Pointer mark created with `::after` pseudo-element in amber.

---

### Step Button (`StepButton`)

Sequencer grid cell.

```astro
<StepButton active={true} beat={true} ariaLabel="Step 1 on" />
```

**CSS**:
```css
.rb-step-btn {
  aspect-ratio: 1;
  min-width: 28px;
  min-height: 28px;
  border: 1px solid var(--rb-border);
  border-radius: 2px;
  background: linear-gradient(180deg, #3a3a3a 0%, #2a2a2a 100%);
}

.rb-step-btn.active {
  background: linear-gradient(180deg, #ffb000 0%, #a07800 100%);
  box-shadow: 0 0 6px rgba(255,176,0,0.4);
}

.rb-step-btn.active--green {
  background: linear-gradient(180deg, #39ff14 0%, #1a7a33 100%);
}

.rb-step-btn.beat {
  background: linear-gradient(180deg, #c41e3a 0%, #8a1528 100%);
}
```

**Responsive**: On viewports below 768px, the 16-step grid collapses to 8 visible steps (steps 9–16 hidden via `:nth-child(n+9) { display: none; }`).

---

### Action Buttons (`ActionButton`)

Two variants: red "pattern" and green "mix".

```astro
<ActionButton variant="pattern" href="/archive/songs">Browse Songs</ActionButton>
<ActionButton variant="mix" href="/archive/mods">Browse Mods</ActionButton>
```

**Pattern (red)**:
```css
background: linear-gradient(180deg, #d42040 0%, #a0182e 60%, #8a1525 100%);
border-bottom: 3px solid #600a18;
```

**Mix (green)**:
```css
background: linear-gradient(180deg, #339958 0%, #236e3e 60%, #1a5230 100%);
border-bottom: 3px solid #0d3020;
```

**Both**: 6px LED dot pseudo-element, `min-height: 36px`, `translateY(-1px)` on hover with glow shadow.

---

### Transport Button (`TransportButton`)

Square hardware transport controls (play/rec/stop).

```astro
<TransportButton variant="play" href="/archive/songs" ariaLabel="Browse Songs">▶</TransportButton>
```

**CSS**: 40×40px square, `border-radius: 4px`, gradient `#3a3a3a` → `#2a2a2a`. Variants tint the icon color: `play` = green, `rec` = red.

---

### File Card (`ModCard` / `ModBrowserCard`)

Content card for songs/mods.

**Structure**:
- Left accent border (3px, green for `.rb-card--song`, amber for `.rb-card--mod`)
- Header: badge + title
- Description paragraph
- Footer: author + tags + link

**Hover**: Border color brightens to full accent, box-shadow deepens.

---

## 4. Spacing & Sizing Scale

### Base Unit

The spacing system is loosely based on `0.25rem` (4px) increments, but pragmatic values are used throughout:

| Token | Value | Usage |
|-------|-------|-------|
| `space-xs` | `0.25rem` (4px) | Step grid gaps, tight padding |
| `space-sm` | `0.5rem` (8px) | Button gaps, inline spacing |
| `space-md` | `0.75rem` (12px) | Panel body padding, card gaps |
| `space-lg` | `1rem` (16px) | Section gaps, container padding |
| `space-xl` | `1.5rem` (24px) | Panel margins, section separation |
| `space-2xl` | `2rem` (32px) | Footer padding, major spacing |
| `space-3xl` | `4rem` (64px) | Page vertical padding |

### Border Radius Scale

| Token | Value | Usage |
|-------|-------|-------|
| `radius-sm` | `2px` | Step buttons, tags, badges |
| `radius-md` | `3px` | Buttons, inputs, small cards |
| `radius-lg` | `4px` | LCDs, transport buttons |
| `radius-xl` | `5px` | File cards |
| `radius-2xl` | `6px` | Hero panels, mod cards |
| `radius-3xl` | `8px` | Main panels, collection cards |

### Container

```css
.container {
  max-width: 1100px;
  margin-inline: auto;
  padding-inline: 1rem;
}
```

### Touch Target Minimums

| Element | Minimum Size |
|---------|-------------|
| Action buttons | 36px height |
| Filter/tag buttons | 36px × 44px |
| Transport buttons | 40px × 40px |
| Step buttons | 28px × 28px |
| Folder links (mobile) | 44px height |
| Nav buttons | ~32px height (desktop) |

---

## 5. Animation Principles

### Philosophy

Animations should feel like **physical hardware** responding to electricity:
- Fast, snappy state changes (100–150ms)
- Glow pulses for "live" indicators
- Mechanical translate-Y for button presses
- Staggered entrance for panel racks

### Timing Tokens

| Context | Duration | Easing |
|---------|----------|--------|
| Button hover / press | `100ms`–`120ms` | `ease` |
| Border/shadow transitions | `120ms` | `ease` |
| Knob rotation | `300ms` | `cubic-bezier(0.22, 1, 0.36, 1)` |
| Panel entrance | `500ms` | `ease-out` |
| LED pulse | `2s` | `ease-in-out` infinite |
| LED flicker | `1.5s` | `steps(8, end)` infinite |
| Cursor blink | `1s` | `step-end` infinite |

### Panel Entrance Animation

```css
@keyframes rb-panel-in {
  from { opacity: 0; transform: translateY(20px); }
  to   { opacity: 1; transform: translateY(0); }
}

.rb-panel--animate {
  animation: rb-panel-in 0.5s ease-out both;
}
```

**Stagger**: Panels use `animation-delay` increments of `0.05s` per sibling index (up to 7 children).

### Button Interaction

1. **Hover**: `translateY(-1px)`, brighter gradient, glow shadow appears
2. **Focus-visible**: Same as hover + amber outline ring
3. **Active/press**: `translateY(1px)`, border-bottom shrinks to 1px, shadow tightens

### LED Animations

**Pulse** (online/pending):
```css
@keyframes rb-pulse-green {
  0%, 100% { opacity: 1; box-shadow: 0 0 6px var(--rb-green-glow), 0 0 12px var(--rb-green-glow); }
  50% { opacity: 0.7; box-shadow: 0 0 2px var(--rb-green-glow), 0 0 4px var(--rb-green-glow); }
}
```

**Flicker** (error/loading):
```css
@keyframes rb-flicker-amber {
  0%, 19%, 21%, 23%, 25%, 54%, 56%, 100% { opacity: 1; /* full glow */ }
  20%, 24%, 55% { opacity: 0.4; /* dim */ }
}
```

Uses `steps(8, end)` for a stuttering electrical-fault feel.

### Global Effects

**Scanline overlay** (`body::before`):
```css
background: repeating-linear-gradient(
  0deg,
  transparent,
  transparent 2px,
  rgba(0,0,0,0.06) 2px,
  rgba(0,0,0,0.06) 4px
);
```

**Vignette** (`body::after`):
```css
background: radial-gradient(ellipse at center, transparent 60%, rgba(0,0,0,0.4) 100%);
```

Both are `pointer-events: none` and `z-index: 9998–9999`.

---

## 6. Layout Patterns

### Grid Systems

```css
/* Auto-fill card grids */
.card-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
  gap: 1rem;
}

/* Fixed column about modules */
.about-row {
  display: grid;
  gap: 1rem;
}
/* Mobile: 1 col | Tablet: 2 col | Desktop: 3 col */

/* LCD stat bar */
.lcd-stats {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0.6rem;
}
/* Desktop: 2×2 or 4×1 depending on parent width */
```

### Responsive Breakpoints

| Name | Width | Behavior |
|------|-------|----------|
| Mobile | `< 768px` | Single column, stacked instrument strip, 8-step sequencer, hamburger nav |
| Tablet | `768px – 1023px` | 2-column grids, full nav visible |
| Desktop | `≥ 768px` | 2-column hero body, multi-column layouts |
| Wide | `≥ 1024px` | 3-column about modules, auto-fill card grids |

**Breakpoints are consistently at `768px`** (not 800px). Use `min-width: 768px` for desktop-up and `max-width: 767px` for mobile-only.

---

## 7. Accessibility Rules

1. **Focus-visible**: All interactive elements use `:focus-visible` (not `:focus`) with amber ring or glow
2. **Skip nav**: `.skip-nav` link provided in `BaseLayout`
3. **aria-label**: LEDs, LCDs, and icon-only buttons always have accessible labels
4. **aria-hidden**: Decorative screws, step sequencer numbers, and visual-only dots are hidden from AT
5. **Contrast**: Amber on dark passes WCAG 2.1 AA; dim silver (`#888`) is never used for essential text
6. **Touch targets**: All buttons meet 36px minimum; folder links on mobile meet 44px

---

## 8. File Locations

| System | Path |
|--------|------|
| Global CSS | `public/styles/rebirth-theme.css` |
| Global reset / tokens | `src/styles/global.css` |
| UI components | `src/components/ui/` |
| Component types | `src/components/ui/types.ts` |
| This document | `docs/DESIGN_SYSTEM.md` |
