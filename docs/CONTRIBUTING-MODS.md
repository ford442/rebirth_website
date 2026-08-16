# Contributing Mod Metadata

The mod archive contains **367 `.rbm` files**. As of the current milestone, **101 mods** have metadata entries with titles and tags; the goal is **200+** over time.

## Quick contribution path

1. Find an undocumented mod in the [complete mod index](https://ford442.github.io/rebirth_website/archive/mods).
2. Click **“Help document this mod”** on a raw index row to open a pre-filled GitHub issue.
3. Fill in as many fields as you can (only `filename` and `title` are required).
4. Submit the issue. A maintainer will add the entry to `src/data/mods-metadata.json`.

## JSON schema

Each entry lives in `src/data/mods-metadata.json` under the `mods` array.

```json
{
  "filename": "Aluwerk.rbm",
  "title": "Aluwerk",
  "author": "Dr Bruno & Syrusate",
  "year": null,
  "description": "A true concept-mod with a metallic look and sound, highly appealing to producers of industrial music.",
  "tags": ["industrial", "metallic", "concept"]
}
```

### Required fields

| Field      | Type     | Description                                                                               |
| ---------- | -------- | ----------------------------------------------------------------------------------------- |
| `filename` | string   | Exact filename as it appears in `src/data/mods-full-index.json` and on `storage.1ink.us`. |
| `title`    | string   | Human-readable mod name.                                                                  |
| `tags`     | string[] | At least one tag from the controlled vocabulary below.                                    |

### Optional fields

| Field         | Type            | Description                                                   |
| ------------- | --------------- | ------------------------------------------------------------- |
| `author`      | string          | Creator, group, or source. Use `"Unknown"` when uncertain.    |
| `year`        | integer \| null | Release year if known.                                        |
| `description` | string          | One or two sentences about the mod’s look, sound, or history. |

## Controlled tag vocabulary

Use **lowercase kebab-case** tags only. Pick from this list; propose new tags via PR if none fit.

### Origin / status

| Tag                  | Use when                                               |
| -------------------- | ------------------------------------------------------ |
| `official`           | Bundled with ReBirth 2.0.x CD or Propellerhead release |
| `default`            | One of the four factory default mods                   |
| `classic`            | Widely cited community or factory staple               |
| `community-classic`  | Famous fan-made mod (Peff archive, NordBeat, etc.)     |
| `campaign`           | Commercial/promotional mod (e.g. Red Stripe)           |
| `contest`            | Song/mod contest entry                                 |
| `beta`               | Beta, dev, or pre-release build                        |
| `version-controlled` | Multiple numbered revisions of the same mod lineage    |

### Genre / sound

| Tag          | Use when                               |
| ------------ | -------------------------------------- |
| `acid`       | TB-303 / acid bass focus               |
| `303`        | Explicit 303 tribute or mapping        |
| `house`      | House / deep-house oriented            |
| `techno`     | Techno / minimal techno                |
| `trance`     | Trance / hard trance                   |
| `dnb`        | Drum & bass / jungle                   |
| `industrial` | Industrial / EBM / noise               |
| `funk`       | Funk / breakbeat                       |
| `jazz`       | Jazz / acid-jazz                       |
| `acid-jazz`  | Acid jazz specifically                 |
| `ambient`    | Downtempo / atmospheric                |
| `dub`        | Dub / reggae influence                 |
| `metal`      | Metal / hard rock drums                |
| `rock`       | Rock-oriented percussion               |
| `breaks`     | Breakbeat programming                  |
| `electro`    | Electro / synthetic                    |
| `electronic` | General electronic / synthetic palette |
| `dark`       | Dark / gothic aesthetic                |
| `darkwave`   | Darkwave / gothic electronic           |
| `deep-house` | Deep house specifically                |
| `hip-hop`    | Hip-hop / boom-bap drums               |
| `rave`       | 90s rave / hard dance                  |
| `minimal`    | Minimal / sparse design                |

### Visual / concept

| Tag          | Use when                      |
| ------------ | ----------------------------- |
| `space`      | Cosmic / orbital theme        |
| `futuristic` | Sci-fi / Y2K UI               |
| `themed`     | Pop-culture or game tie-in    |
| `gaming`     | Video-game themed mod         |
| `sci-fi`     | Science-fiction artwork       |
| `concept`    | Strong visual concept mod     |
| `metallic`   | Metal / chrome GUI            |
| `clean`      | Clean / polished modern sound |

### Technical

| Tag                  | Use when                                 |
| -------------------- | ---------------------------------------- |
| `tribute`            | Homage to hardware or artist             |
| `hardware-emulation` | Emulates Roland/other drum machines      |
| `808`                | TR-808 focus                             |
| `909`                | TR-909 focus                             |
| `707`                | TR-707 focus                             |
| `727`                | TR-727 focus                             |
| `sampler-tribute`    | Emulates SP-1200, etc.                   |
| `chiptune`           | SID / 8-bit character                    |
| `experimental`       | Unusual or uncategorised (fallback)      |
| `analog`             | Analog synth / Oberheim-style character  |
| `synth`              | Synthesizer-forward drum or bass mapping |

### Misc

| Tag             | Use when                                        |
| --------------- | ----------------------------------------------- |
| `percussion`    | Percussion-forward design                       |
| `bass`          | Sub / bass emphasis                             |
| `pads`          | Pad / chord samples                             |
| `drums`         | General drum-kit swap                           |
| `celebration`   | Anniversary / event mod                         |
| `branding`      | Agency or product branding                      |
| `tribal`        | Tribal / ritual percussion                      |
| `world`         | World / ethnic fusion                           |
| `ethnic`        | Ethnic instrumentation focus                    |
| `sitar`         | Sitar / South Asian instrumentation             |
| `fast`          | High-BPM signature (e.g. Red Stripe at 338 BPM) |
| `guitar`        | Distorted or live guitar textures               |
| `toolbox`       | Producer utility / sample toolkit mod           |
| `organ`         | Organ / keyboard samples                        |
| `piano`         | Piano / keys focus                              |
| `brass`         | Brass / horn samples                            |
| `original`      | Original / first-release lineage                |
| `beat`          | Beat-forward programming                        |
| `funky`         | Funky / groove-oriented                         |
| `minimalist`    | Ultra-minimal design                            |
| `nord`          | Nordic / NordBeat scene                         |
| `award-winning` | Contest or publication winner                   |
| `collaboration` | Multi-author collaboration                      |
| `development`   | Dev / work-in-progress lineage                  |
| `control`       | Control-surface or MIDI-oriented                |
| `bleeps`        | Bleep / lo-fi digital character                 |
| `chilly`        | Cold / icy aesthetic                            |
| `varied`        | Varied multi-genre sample set                   |
| `sharp`         | Sharp / crisp digital transients                |
| `powerful`      | Heavy / powerful drum hits                      |
| `vision`        | Visual concept / UI showcase                    |

## Semi-automated batch workflow

Maintainers can draft entries from filenames (human review required):

```bash
# Preview inferred titles/tags for the next batch
python3 scripts/infer-mod-metadata.py --draft drafts.json --target 120 --priority

# Famous mods with rich descriptions live in:
#   scripts/mod-metadata-overrides.json

# Apply reviewed batch (updates metadata + baseline)
python3 scripts/infer-mod-metadata.py --apply --target 120 --priority --update-baseline

# Sync badges in the full index
python3 scripts/sync-mod-metadata.py
```

Title inference rules (`scripts/infer-mod-metadata.py`):

- Strip `.rbm`, replace `_` with spaces, preserve acronyms (PBE, MSM, TB-909).
- Tags are keyword-matched against the controlled vocabulary above.
- Overrides file takes precedence for famous mods (Orbit, PBE, MSM, NordBeat, etc.).

Optional historical sources for descriptions:

- [peff.com mod journal](https://www.peff.com/journal/rebirth-mods/) (Wayback)
- [ReBirth Resource Roundup](../src/content/docs/rebirth-resource-roundup.md) notable-mod list
- Site history page mod section (`/history`)

Extract roundup hints into a reviewable JSON file:

```bash
python3 scripts/fetch-peff-mod-hints.py --output scripts/peff-mod-hints.json
```

## Batch submissions

```bash
python3 scripts/check-mod-metadata.py
python3 scripts/check-mod-metadata.py --priority --output priority-mods.txt
python3 scripts/check-mod-metadata.py --json-output gaps.json
```

The `--priority` flag surfaces official/default mods and larger files first.

## CI coverage baseline

`scripts/mod-metadata-baseline.json` stores the minimum documented count (currently **101**). CI runs:

```bash
python3 scripts/check-mod-metadata.py --priority
```

This warns (non-blocking) when:

- Undocumented gaps remain (expected until 367/367)
- Documented count **drops below the baseline** (regression)

After a deliberate metadata milestone, update the baseline:

```bash
python3 scripts/infer-mod-metadata.py --apply --target 150 --update-baseline
```

## Validation

Before opening a PR:

```bash
python3 -m json.tool src/data/mods-metadata.json > /dev/null && echo "JSON OK"
python3 scripts/sync-mod-metadata.py
python3 scripts/check-mod-metadata.py --no-fail-on-gaps
npm run build
```

## Workflow

1. Edit `src/data/mods-metadata.json` (or use `infer-mod-metadata.py --apply`).
2. Run `python3 scripts/sync-mod-metadata.py`.
3. Run `npm run dev` and visit `/archive/mods` to verify cards and filters.
4. Open a PR noting how many mods were documented and any famous entries added.

## Milestones

| Target              | Status     |
| ------------------- | ---------- |
| 100 documented mods | Done (101) |
| 200 documented mods | Next goal  |
| 367 / full coverage | Long-term  |
