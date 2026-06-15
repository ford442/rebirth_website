#!/usr/bin/env python3
"""
sync-mod-metadata.py

Align `src/data/mods-full-index.json` with `src/data/mods-metadata.json`.
Sets `hasMeta: true` and copies `title`, `author`, and `tags` for every mod
that has a metadata entry. This keeps the raw index badges and the coverage
progress bar in sync.

Usage:
    python3 scripts/sync-mod-metadata.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

METADATA_PATH = Path("src/data/mods-metadata.json")
FULL_INDEX_PATH = Path("src/data/mods-full-index.json")


def load_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path: Path, data: dict) -> None:
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def main() -> int:
    metadata = load_json(METADATA_PATH)
    full_index = load_json(FULL_INDEX_PATH)

    meta_by_filename = {m["filename"]: m for m in metadata.get("mods", [])}

    synced = 0
    for mod in full_index.get("mods", []):
        filename = mod.get("filename")
        meta = meta_by_filename.get(filename)
        if meta:
            mod["hasMeta"] = True
            mod["title"] = meta.get("title", mod.get("title"))
            mod["author"] = meta.get("author", mod.get("author"))
            mod["tags"] = meta.get("tags", mod.get("tags", []))
            synced += 1
        elif mod.get("hasMeta"):
            # Remove stale meta flags for files no longer in metadata
            mod["hasMeta"] = False
            mod.pop("title", None)
            mod.pop("author", None)
            mod.pop("tags", None)

    save_json(FULL_INDEX_PATH, full_index)

    print(f"Synced {synced} mods in {FULL_INDEX_PATH}")
    print(
        f"Coverage: {synced} / {len(full_index.get('mods', []))} "
        f"({round(synced / len(full_index.get('mods', [])) * 100, 1)}%)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
