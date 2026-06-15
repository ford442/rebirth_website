#!/usr/bin/env python3
"""
check-mod-metadata.py

Diff `src/data/mods-full-index.json` against `src/data/mods-metadata.json` and report
undocumented `.rbm` files. Useful for batch contribution drives and CI checks.

Usage:
    python3 scripts/check-mod-metadata.py
    python3 scripts/check-mod-metadata.py --output undocumented.txt
    python3 scripts/check-mod-metadata.py --priority --output priority.txt
    python3 scripts/check-mod-metadata.py --json-output gaps.json

Exit codes:
    0  All files are documented.
    1  One or more files lack metadata (also prints a warning).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

METADATA_PATH = Path("src/data/mods-metadata.json")
FULL_INDEX_PATH = Path("src/data/mods-full-index.json")

# Filenames that look like they may be official or default mods.
PRIORITY_KEYWORDS = re.compile(r"official|default|propellerhead|propeller|peff|metallicon|red[_\s]?stripe", re.I)


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


def load_json(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Report undocumented .rbm mod files."
    )
    parser.add_argument(
        "--metadata",
        default=METADATA_PATH,
        type=Path,
        help=f"Path to mods-metadata.json (default: {METADATA_PATH})",
    )
    parser.add_argument(
        "--full-index",
        default=FULL_INDEX_PATH,
        type=Path,
        help=f"Path to mods-full-index.json (default: {FULL_INDEX_PATH})",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write undocumented filenames to this file (one per line).",
    )
    parser.add_argument(
        "--json-output",
        type=Path,
        help="Write detailed JSON report to this file.",
    )
    parser.add_argument(
        "--priority",
        action="store_true",
        help="Sort by priority: official/default-sounding names first, then largest files.",
    )
    args = parser.parse_args()

    metadata = load_json(args.metadata)
    full_index = load_json(args.full_index)

    documented = {m["filename"] for m in metadata.get("mods", [])}
    all_mods = full_index.get("mods", [])
    total = len(all_mods)
    documented_count = len(documented)
    full_set = {m["filename"] for m in all_mods}
    undocumented = [m for m in all_mods if m["filename"] not in documented]
    stale = sorted(documented - full_set)

    coverage = (documented_count / total * 100) if total else 0.0

    print(f"Total .rbm files:   {total}")
    print(f"Documented:         {documented_count}")
    print(f"Undocumented:       {len(undocumented)}")
    print(f"Coverage:           {coverage:.1f}%")
    if stale:
        print(f"Stale entries:      {len(stale)}")
        for name in stale:
            print(f"  - {name} (in metadata but not in full index)")

    if args.priority:
        undocumented.sort(
            key=lambda m: (
                0 if PRIORITY_KEYWORDS.search(m["filename"]) else 1,
                -parse_size(m.get("size", "")),
                m["filename"].lower(),
            )
        )
    else:
        undocumented.sort(key=lambda m: m["filename"].lower())

    filenames = [m["filename"] for m in undocumented]

    if args.output:
        args.output.write_text("\n".join(filenames) + "\n", encoding="utf-8")
        print(f"Wrote {len(filenames)} filenames to {args.output}")

    if args.json_output:
        report = {
            "total": total,
            "documented": documented_count,
            "undocumented": len(undocumented),
            "coveragePercent": round(coverage, 1),
            "files": filenames,
        }
        args.json_output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON report to {args.json_output}")

    if undocumented:
        print("\nUndocumented files:")
        for name in filenames:
            print(f"  - {name}")
        print(
            "\nTo document a mod, open an issue: "
            "https://github.com/ford442/rebirth_website/issues/new?template=mod-metadata.yml",
            file=sys.stderr,
        )
        return 1

    print("\nAll .rbm files have metadata entries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
