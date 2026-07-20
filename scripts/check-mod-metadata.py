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
    0  Documented count meets baseline and no regression.
    1  Undocumented gaps remain OR documented count dropped below baseline.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

METADATA_PATH = Path("src/data/mods-metadata.json")
FULL_INDEX_PATH = Path("src/data/mods-full-index.json")
BASELINE_PATH = Path("scripts/mod-metadata-baseline.json")
VOCAB_PATH = Path("docs/CONTRIBUTING-MODS.md")

# Filenames that look like they may be official or default mods.
PRIORITY_KEYWORDS = re.compile(
    r"official|default|propellerhead|propeller|peff|metallicon|red[_\s-]?stripe|"
    r"orbit|pbe|msm|nordbeat|midimod",
    re.I,
)


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


def load_baseline(path: Path) -> int | None:
    if not path.exists():
        return None
    data = load_json(path)
    value = data.get("minDocumented")
    return int(value) if isinstance(value, int) else None


def load_controlled_tags(path: Path) -> set[str]:
    """Parse tag names from vocabulary tables in CONTRIBUTING-MODS.md."""
    if not path.exists():
        return set()
    tags: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"\| `([a-z0-9][a-z0-9-]*)`", line)
        if match:
            tags.add(match.group(1))
    return tags


def find_unknown_tags(metadata: dict, allowed: set[str]) -> list[tuple[str, str]]:
    unknown: list[tuple[str, str]] = []
    for mod in metadata.get("mods", []):
        for tag in mod.get("tags") or []:
            if tag not in allowed:
                unknown.append((mod["filename"], tag))
    return unknown


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
        "--baseline",
        default=BASELINE_PATH,
        type=Path,
        help=f"Minimum documented count baseline (default: {BASELINE_PATH})",
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
    parser.add_argument(
        "--fail-on-gaps",
        action="store_true",
        default=True,
        help="Exit 1 when undocumented files remain (default: true).",
    )
    parser.add_argument(
        "--no-fail-on-gaps",
        action="store_false",
        dest="fail_on_gaps",
        help="Always exit 0 for gap count (still fails on baseline regression).",
    )
    parser.add_argument(
        "--validate-tags",
        action="store_true",
        default=True,
        help="Warn when metadata uses tags outside CONTRIBUTING-MODS.md (default: true).",
    )
    parser.add_argument(
        "--no-validate-tags",
        action="store_false",
        dest="validate_tags",
        help="Skip controlled-vocabulary tag checks.",
    )
    parser.add_argument(
        "--strict-tags",
        action="store_true",
        help="Exit 1 when unknown tags are found.",
    )
    args = parser.parse_args()

    metadata = load_json(args.metadata)
    full_index = load_json(args.full_index)
    min_documented = load_baseline(args.baseline)

    documented = {m["filename"] for m in metadata.get("mods", [])}
    all_mods = full_index.get("mods", [])
    total = len(all_mods)
    documented_count = len(documented)
    full_set = {m["filename"] for m in all_mods}
    undocumented = [m for m in all_mods if m["filename"] not in documented]
    stale = sorted(documented - full_set)

    coverage = (documented_count / total * 100) if total else 0.0
    missing_tags = [
        m["filename"]
        for m in metadata.get("mods", [])
        if m["filename"] in full_set and not m.get("tags")
    ]

    print(f"Total .rbm files:   {total}")
    print(f"Documented:         {documented_count}")
    print(f"Undocumented:       {len(undocumented)}")
    print(f"Coverage:           {coverage:.1f}%")
    if min_documented is not None:
        print(f"Baseline minimum:   {min_documented}")
    if stale:
        print(f"Stale entries:      {len(stale)}")
        for name in stale:
            print(f"  - {name} (in metadata but not in full index)")
    if missing_tags:
        print(f"Missing tags:       {len(missing_tags)}")

    unknown_tag_pairs: list[tuple[str, str]] = []
    if args.validate_tags:
        allowed_tags = load_controlled_tags(VOCAB_PATH)
        unknown_tag_pairs = find_unknown_tags(metadata, allowed_tags)
        if unknown_tag_pairs:
            unique_unknown = sorted({tag for _, tag in unknown_tag_pairs})
            print(f"Unknown tags:       {len(unique_unknown)} ({len(unknown_tag_pairs)} usages)")
            for tag in unique_unknown[:12]:
                print(f"  - {tag}")
            if len(unique_unknown) > 12:
                print(f"  … and {len(unique_unknown) - 12} more")

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
            "minDocumented": min_documented,
            "files": filenames,
        }
        args.json_output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Wrote JSON report to {args.json_output}")

    exit_code = 0

    if min_documented is not None and documented_count < min_documented:
        print(
            f"\nWARNING: Documented count {documented_count} dropped below baseline {min_documented}.",
            file=sys.stderr,
        )
        exit_code = 1

    if args.strict_tags and unknown_tag_pairs:
        print(
            f"\nWARNING: {len(unknown_tag_pairs)} tag(s) outside controlled vocabulary.",
            file=sys.stderr,
        )
        exit_code = 1

    if undocumented:
        print("\nUndocumented files:")
        for name in filenames[:20]:
            print(f"  - {name}")
        if len(filenames) > 20:
            print(f"  … and {len(filenames) - 20} more")
        print(
            "\nTo document a mod, open an issue: "
            "https://github.com/ford442/rebirth_website/issues/new?template=mod-metadata.yml",
            file=sys.stderr,
        )
        if args.fail_on_gaps:
            exit_code = 1
    else:
        print("\nAll .rbm files have metadata entries.")

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
