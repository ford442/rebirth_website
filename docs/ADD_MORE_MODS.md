# Adding More Mods to the Archive

> For the full contributor workflow, schema, and tag conventions, see [`CONTRIBUTING-MODS.md`](CONTRIBUTING-MODS.md).

## Quick Start

The 367 RBM mods are already hosted at `https://storage.1ink.us/rebirth_mods/` and automatically downloadable. The mods in `src/data/mods-metadata.json` are displayed in the featured section; the rest appear in the raw index with a **“Help document this mod”** link.

To add more mods:

## Option 1: Minimal Metadata (Fast)

If you have just filenames, create basic entries **without descriptions**:

```json
{
  "filename": "030microdot.rbm",
  "title": "030 Microdot",
  "author": "Unknown",
  "year": null,
  "tags": []
}
```

Descriptions are optional! Mods without descriptions will still be displayed with a subtle visual indicator. Contributors can enhance them later with real metadata.

## Option 2: Batch Import from Directory

To auto-populate entries from the remote mod directory (without descriptions):

```bash
# 1. Fetch the mod listing (if server provides it)
curl https://storage.1ink.us/rebirth_mods/ -l 2>/dev/null | grep ".rbm" | \
  sed 's/.*>\([^<]*\.rbm\)<.*/\1/' | sort > /tmp/all_mods.txt

# 2. Create JSON entries for all files (without descriptions)
node -e "
const fs = require('fs');
const mods = fs.readFileSync('/tmp/all_mods.txt', 'utf-8').trim().split('\n');
const entries = mods.map(filename => ({
  filename,
  title: filename.replace('.rbm', '').replace(/[-_]/g, ' '),
  author: 'Unknown',
  year: null,
  tags: []
}));
console.log(JSON.stringify({ mods: entries }, null, 2));
" > src/data/mods-metadata.json
```

## Option 3: Parse Historical Archives

You may have metadata from historical ReBirth forums or archives. If so, create a mapping:

```json
{
  "filename": "AFX_1.rbm",
  "title": "AFX 1",
  "author": "AFX (Autechre?)",
  "year": 1999,
  "description": "Glitchy electronic mod inspired by IDM and generative music.",
  "tags": ["idm", "glitch", "electronic", "experimental"]
}
```

## Adding Metadata From Your List

Based on the filenames you provided, here are some examples to start:

```json
{
  "filename": "030microdot.rbm",
  "title": "030 Microdot",
  "author": "Unknown",
  "description": "RBM mod file - awaiting documentation",
  "tags": []
},
{
  "filename": "5emedia_1.828.rbm",
  "title": "5emedia 1.828",
  "author": "5emedia",
  "description": "Professional branding campaign mod (v1.828)",
  "tags": ["professional", "campaign"]
},
{
  "filename": "707&727.rbm",
  "title": "707 & 727",
  "author": "Unknown",
  "description": "Tribute to Roland drum machines",
  "tags": ["drum", "tribute", "roland"]
},
{
  "filename": "7mbFunkbox_v1.2.rbm",
  "title": "7MB Funkbox v1.2",
  "author": "Unknown",
  "description": "Funk-focused toolbox mod",
  "tags": ["funk", "house", "toolbox"]
}
```

## Tag Suggestions for Common Types

Review the filenames and apply appropriate tags:

| Pattern                               | Suggested Tags              |
| ------------------------------------- | --------------------------- |
| Contains "funk" or "groove"           | funky, house, groove        |
| Ends in version numbers (v1.2, Beta)  | version-controlled          |
| Named after hardware (TB-303, TR-909) | tribute, hardware-emulation |
| Word "final", "pro", "ultimate"       | professional, refined       |
| Numbered versions                     | iterative-series            |
| Author names known                    | author-verified             |
| Experimental filenames                | experimental                |

## Workflow

1. **Add entries** to `src/data/mods-metadata.json`
2. **Test locally**: `npm run dev`
3. **Build**: `npm run build`
4. **Preview**: `npm run preview`
5. **Deploy**: Push to `main` branch (auto-deploys via GitHub Pages)

## File Size Reference

From your list, file sizes range from **130 KB to 8.9 MB**.

Large mods (7+ MB):

- 030microdot.rbm (8.1M)
- Prop10_Dev.rbm (8.9M)
- ice_RB20Mod.rbm (6.6M)
- Controllerism.rbm (8.4M)
- ElekD2.02.rbm (7.2M)

These might be feature-rich or include extensive sound libraries.

## Community Contributions

After setting up the initial list, you can accept community metadata contributions:

1. Create GitHub issue template for "Add Mod Metadata"
2. Users submit: filename + title + author + description + tags
3. Maintainer adds to JSON and rebuilds

## Performance Note

With 600+ mods, the JSON file will be ~500 KB. This is fine for static generation but consider:

- **Pagination**: Display 50 mods per "page" if search is slow
- **Categories**: Split into separate JSON files by genre
- **API**: Generate a simple JSON API endpoint from metadata

For now, all ~600 entries embedded in one JSON file should work fine.

---

**Next Step**: Edit `src/data/mods-metadata.json` and add entries for the mods most commonly requested by your community.
