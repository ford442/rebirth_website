# Contributing Mod Metadata

The mod archive contains **367 `.rbm` files**, but only a small percentage have rich metadata. Adding metadata makes the archive searchable, browsable, and useful for newcomers.

## Quick contribution path

1. Find an undocumented mod in the [complete mod index](https://ford442.github.io/rb338/archive/mods).
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

| Field | Type | Description |
|-------|------|-------------|
| `filename` | string | Exact filename as it appears in `src/data/mods-full-index.json` and on `storage.1ink.us`. |
| `title` | string | Human-readable mod name. |

### Optional fields

| Field | Type | Description |
|-------|------|-------------|
| `author` | string | Creator, group, or source. Use `"Unknown"` when uncertain. |
| `year` | integer \| null | Release year if known. |
| `description` | string | One or two sentences about the mod’s look, sound, or history. |
| `tags` | string[] | Lowercase kebab-case tags. See conventions below. |

## Tag conventions

Keep tags short, lowercase, and hyphenated.

| Pattern | Suggested tags |
|---------|----------------|
| Official Propellerhead mod | `official`, `default`, `classic` |
| Industrial / dark sound | `industrial`, `dark`, `metal` |
| House / techno oriented | `house`, `techno`, `funky` |
| World / ethnic instruments | `ethnic`, `world`, `sitar`, `tribal` |
| Versioned filename (`v1.2`, `Beta`) | `version-controlled` |
| Hardware tribute (`TB-303`, `TR-909`) | `tribute`, `hardware-emulation` |
| Experimental / unusual | `experimental` |

## Batch submissions

If you want to document many mods at once, run the metadata gap script:

```bash
python3 scripts/check-mod-metadata.py
python3 scripts/check-mod-metadata.py --output undocumented-mods.txt
python3 scripts/check-mod-metadata.py --priority --output priority-mods.txt
```

The `--priority` flag surfaces official/default mods and larger files first, which are likely the most sought-after by the community.

## Validation

Before opening a PR, make sure your JSON is valid and the filename exists in `src/data/mods-full-index.json`:

```bash
python3 -m json.tool src/data/mods-metadata.json > /dev/null && echo "JSON OK"
python3 scripts/check-mod-metadata.py
```

## Workflow

1. Edit `src/data/mods-metadata.json`.
2. Run `npm run dev` and visit `/archive/mods` to see the new cards.
3. Run `npm run build`.
4. Open a PR with a short note about the mods you documented.
