#!/usr/bin/env python3
"""
infer-mod-metadata.py

Semi-automated mod metadata generation for undocumented .rbm files.

1. Applies curated overrides from scripts/mod-metadata-overrides.json (famous mods).
2. Infers human-readable titles from filenames (review recommended).
3. Suggests tags from a controlled vocabulary based on filename keywords.
4. Optionally merges drafts into src/data/mods-metadata.json.

Usage:
    python3 scripts/infer-mod-metadata.py --draft --target 100
    python3 scripts/infer-mod-metadata.py --apply --target 100
    python3 scripts/infer-mod-metadata.py --apply --target 100 --priority
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

METADATA_PATH = Path("src/data/mods-metadata.json")
FULL_INDEX_PATH = Path("src/data/mods-full-index.json")
OVERRIDES_PATH = Path("scripts/mod-metadata-overrides.json")
BASELINE_PATH = Path("scripts/mod-metadata-baseline.json")

PRIORITY_KEYWORDS = re.compile(
    r"official|default|propellerhead|propeller|peff|metallicon|red[_\s-]?stripe|"
    r"orbit|pbe|msm|nordbeat|midimod|regulator|housemod|rb-sem|reverse808|polar|"
    r"alpha|groove|darkstar|beatnik|millennium|omega|plasmafire|technobox|trancer|"
    r"voltage|sonor|axiom|bad.?rat|5emedia",
    re.I,
)

# Controlled vocabulary — keep in sync with docs/CONTRIBUTING-MODS.md
TAG_KEYWORDS: list[tuple[re.Pattern[str], list[str]]] = [
    (re.compile(r"official|default|propeller|prop10", re.I), ["official"]),
    (re.compile(r"peff|minimod|rebirthday", re.I), ["community-classic"]),
    (re.compile(r"red.?stripe|5emedia", re.I), ["campaign", "dnb"]),
    (re.compile(r"orbit|space|cosmic|orion", re.I), ["space", "ambient"]),
    (re.compile(r"pbe|pitch.?black|dunkel|darkstar|dark", re.I), ["dark", "industrial"]),
    (re.compile(r"msm|massen|silver|metallicon|metal|steel", re.I), ["metal", "industrial"]),
    (re.compile(r"nordbeat|nord.?beat", re.I), ["techno", "trance", "community-classic"]),
    (re.compile(r"midimod|industrial|inferno|infernal|oxide|dejenerate", re.I), ["industrial"]),
    (re.compile(r"funk|funky|funkbox", re.I), ["funk"]),
    (re.compile(r"house|houser|sonor", re.I), ["house"]),
    (re.compile(r"techno|trancer|trance", re.I), ["techno"]),
    (re.compile(r"dnb|drum.?n.?bass|5000", re.I), ["dnb"]),
    (re.compile(r"acid|303|rdx", re.I), ["acid", "303"]),
    (re.compile(r"jazz|polyvibe|bluenote", re.I), ["jazz", "acid-jazz"]),
    (re.compile(r"tribal|ashiko|animal", re.I), ["tribal", "world"]),
    (re.compile(r"ethnic|anokha|sitar", re.I), ["ethnic", "world"]),
    (re.compile(r"808|909|707|727|tb-|tr-", re.I), ["hardware-emulation", "tribute"]),
    (re.compile(r"sid|chiptune|8.?bit", re.I), ["chiptune"]),
    (re.compile(r"beta|dev|test|alpha\b", re.I), ["beta", "experimental"]),
    (re.compile(r"star.?wars|quake|bladerunner|game", re.I), ["themed", "gaming"]),
    (re.compile(r"ambient|aqueous|liquid|mellow", re.I), ["ambient"]),
    (re.compile(r"contest", re.I), ["contest"]),
]

ACRONYMS = {
    "pbe", "msm", "rbm", "rb", "tb", "tr", "dnb", "idm", "dj", "mkii", "mkii",
    "sem", "808", "909", "707", "727", "303", "ii", "iii", "iv", "om", "uv",
    "afx", "dj", "rmx", "rdx", "iq", "mod", "dub", "dn", "bass", "bd",
}


def load_json(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def save_json(path: Path, data: dict[str, Any]) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")


def parse_size(size: str) -> int:
    if not size or size == "-":
        return 0
    size = size.strip().upper()
    num = float(size.rstrip("KMG"))
    if size.endswith("K"):
        return int(num * 1024)
    if size.endswith("M"):
        return int(num * 1024 * 1024)
    if size.endswith("G"):
        return int(num * 1024 * 1024 * 1024)
    return int(num)


def infer_title(filename: str) -> str:
    stem = filename[:-4] if filename.lower().endswith(".rbm") else filename
    stem = stem.replace("_", " ")
    stem = re.sub(r"\[(a\d+|b\d+|m|vx\d+a?|l|dev)\]", r" (\1)", stem, flags=re.I)
    stem = re.sub(r"\(([^)]+)\)", r" (\1)", stem)
    stem = re.sub(r"\s+", " ", stem).strip()

    words: list[str] = []
    for raw in stem.split(" "):
        if not raw:
            continue
        lower = raw.lower().strip(".,;:")
        if lower in ACRONYMS or raw.isupper():
            words.append(raw.upper() if len(raw) <= 4 else raw)
        elif re.match(r"^v?\d+(\.\d+)*$", lower):
            words.append(raw)
        elif lower in {"mod", "modbox", "birth"}:
            words.append(raw.capitalize())
        else:
            words.append(raw.capitalize())
    title = " ".join(words)
    title = re.sub(r"\bMod\b(?=\s|$)", "Mod", title)
    return title or filename


def infer_tags(filename: str, title: str) -> list[str]:
    haystack = f"{filename} {title}"
    tags: list[str] = []
    for pattern, additions in TAG_KEYWORDS:
        if pattern.search(haystack):
            for tag in additions:
                if tag not in tags:
                    tags.append(tag)
    if not tags:
        tags = ["experimental"]
    if "version-controlled" not in tags and re.search(r"v?\d+(\.\d+)+|\bbeta\b|\[", filename, re.I):
        tags.append("version-controlled")
    return tags[:6]


def build_inferred_entry(mod: dict[str, Any]) -> dict[str, Any]:
    filename = mod["filename"]
    title = infer_title(filename)
    tags = infer_tags(filename, title)
    entry: dict[str, Any] = {
        "filename": filename,
        "title": title,
        "author": "Unknown",
        "year": None,
        "tags": tags,
    }
    return entry


def sort_candidates(mods: list[dict[str, Any]], priority: bool) -> list[dict[str, Any]]:
    if priority:
        return sorted(
            mods,
            key=lambda m: (
                0 if PRIORITY_KEYWORDS.search(m["filename"]) else 1,
                -parse_size(m.get("size", "")),
                m["filename"].lower(),
            ),
        )
    return sorted(mods, key=lambda m: (-parse_size(m.get("size", "")), m["filename"].lower()))


def merge_entries(existing: list[dict[str, Any]], new_entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_name = {entry["filename"]: entry for entry in existing}
    for entry in new_entries:
        by_name[entry["filename"]] = entry
    return sorted(by_name.values(), key=lambda e: e["filename"].lower())


def main() -> int:
    parser = argparse.ArgumentParser(description="Infer mod metadata drafts from filenames.")
    parser.add_argument("--metadata", type=Path, default=METADATA_PATH)
    parser.add_argument("--full-index", type=Path, default=FULL_INDEX_PATH)
    parser.add_argument("--overrides", type=Path, default=OVERRIDES_PATH)
    parser.add_argument("--target", type=int, default=100, help="Minimum documented mod count.")
    parser.add_argument("--priority", action="store_true", help="Prefer famous/large mods first.")
    parser.add_argument("--draft", type=Path, help="Write draft JSON to this path.")
    parser.add_argument("--apply", action="store_true", help="Merge into mods-metadata.json.")
    parser.add_argument("--update-baseline", action="store_true", help="Write baseline file after apply.")
    args = parser.parse_args()

    metadata = load_json(args.metadata)
    full_index = load_json(args.full_index)
    overrides = load_json(args.overrides) if args.overrides.exists() else {"mods": []}

    documented = {m["filename"] for m in metadata.get("mods", [])}
    all_mods = full_index.get("mods", [])
    full_set = {m["filename"] for m in all_mods}
    stale = sorted(documented - full_set)

    override_map = {m["filename"]: m for m in overrides.get("mods", [])}
    undocumented = [m for m in all_mods if m["filename"] not in documented]
    candidates = sort_candidates(undocumented, args.priority)

    needed = max(0, args.target - len(documented))
    selected: list[dict[str, Any]] = []
    for mod in candidates:
        if len(selected) >= needed:
            break
        filename = mod["filename"]
        if filename in override_map:
            selected.append(override_map[filename])
        else:
            selected.append(build_inferred_entry(mod))

    print(f"Documented now:     {len(documented)}")
    print(f"Target:             {args.target}")
    print(f"New entries:        {len(selected)}")
    print(f"Projected total:    {len(documented) + len(selected)}")
    if stale:
        print(f"Stale (will drop):  {', '.join(stale)}")

    draft = {"mods": selected}
    if args.draft:
        save_json(args.draft, draft)
        print(f"Wrote draft to {args.draft}")

    if args.apply:
        cleaned = [m for m in metadata.get("mods", []) if m["filename"] in full_set]
        merged = merge_entries(cleaned, selected)
        save_json(args.metadata, {"mods": merged})
        print(f"Updated {args.metadata} ({len(merged)} entries)")

        if args.update_baseline:
            baseline = {
                "minDocumented": len(merged),
                "note": "CI warns if documented count drops below minDocumented.",
            }
            save_json(BASELINE_PATH, baseline)
            print(f"Updated baseline {BASELINE_PATH}")

    if not args.apply and not args.draft:
        print("\nSample inferred entries:")
        for entry in selected[:5]:
            print(f"  {entry['filename']}: {entry['title']} — {entry.get('tags', [])}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
