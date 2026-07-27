#!/usr/bin/env python3
"""
fetch-peff-mod-hints.py

Extract mod title/author/description hints from historical docs and optional
Wayback snapshots of peff.com. Output is for human review — merge curated rows
into scripts/mod-metadata-overrides.json before running infer-mod-metadata.py.

Usage:
    python3 scripts/fetch-peff-mod-hints.py
    python3 scripts/fetch-peff-mod-hints.py --output scripts/peff-mod-hints.json
    python3 scripts/fetch-peff-mod-hints.py --wayback --limit 5
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

ROUNDUP_PATH = Path("src/content/docs/rebirth-resource-roundup.md")
FULL_INDEX_PATH = Path("src/data/mods-full-index.json")
DEFAULT_OUTPUT = Path("scripts/peff-mod-hints.json")
WAYBACK_JOURNAL = (
    "https://web.archive.org/web/20040129012338/http://peff.com/journal/rebirth-mods/"
)

ROUNDUP_LINE = re.compile(
    r"^\-\s+\*\*(.+?)\*\*(?:\s+by\s+(.+?))?\s+--\s+(.+)$",
    re.I,
)


def load_json(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def save_json(path: Path, data: dict[str, Any]) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")


def normalize_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", name.lower())


def build_filename_index(mods: list[dict[str, Any]]) -> dict[str, str]:
    index: dict[str, str] = {}
    for mod in mods:
        filename = mod["filename"]
        stem = filename[:-4] if filename.lower().endswith(".rbm") else filename
        index[normalize_name(stem)] = filename
    return index


def match_filename(title: str, index: dict[str, str]) -> str | None:
    key = normalize_name(title)
    if key in index:
        return index[key]
    for norm, filename in index.items():
        if key in norm or norm in key:
            return filename
    return None


def parse_roundup(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    hints: list[dict[str, str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = ROUNDUP_LINE.match(line.strip())
        if not match:
            continue
        title, author, description = match.groups()
        hints.append(
            {
                "title": title.strip(),
                "author": (author or "Unknown").strip(),
                "description": description.strip(),
                "source": "rebirth-resource-roundup.md",
            }
        )
    return hints


def fetch_wayback_snippets(url: str, limit: int) -> list[str]:
    headers = {
        "User-Agent": (
            "Mozilla/5.0 (compatible; rebirth-mod-metadata-bot/1.0; +https://ford442.github.io/rebirth_website)"
        )
    }
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            html = response.read().decode("utf-8", errors="replace")
    except (urllib.error.URLError, TimeoutError) as exc:
        print(f"Wayback fetch failed: {exc}", file=sys.stderr)
        return []

    snippets: list[str] = []
    for match in re.finditer(r">([^<]{20,180})<", html):
        text = re.sub(r"\s+", " ", match.group(1)).strip()
        if ".rbm" in text.lower() or "mod" in text.lower():
            snippets.append(text)
        if len(snippets) >= limit:
            break
    return snippets


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch peff/roundup mod description hints.")
    parser.add_argument("--roundup", type=Path, default=ROUNDUP_PATH)
    parser.add_argument("--full-index", type=Path, default=FULL_INDEX_PATH)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--wayback", action="store_true", help="Append Wayback journal snippets.")
    parser.add_argument("--limit", type=int, default=10, help="Max Wayback snippets.")
    args = parser.parse_args()

    full_index = load_json(args.full_index)
    filename_index = build_filename_index(full_index.get("mods", []))

    entries: list[dict[str, Any]] = []
    unmatched: list[str] = []

    for hint in parse_roundup(args.roundup):
        filename = match_filename(hint["title"], filename_index)
        entry: dict[str, Any] = {
            "title": hint["title"],
            "author": hint["author"],
            "description": hint["description"],
            "source": hint["source"],
        }
        if filename:
            entry["filename"] = filename
            entries.append(entry)
        else:
            unmatched.append(hint["title"])

    wayback_snippets: list[str] = []
    if args.wayback:
        wayback_snippets = fetch_wayback_snippets(WAYBACK_JOURNAL, args.limit)

    report = {
        "source": str(args.roundup),
        "matched": len(entries),
        "unmatchedTitles": unmatched,
        "mods": entries,
        "waybackSnippets": wayback_snippets,
    }

    save_json(args.output, report)
    print(f"Wrote {len(entries)} matched hints to {args.output}")
    if unmatched:
        print(f"Unmatched roundup titles: {len(unmatched)}")
        for title in unmatched[:8]:
            print(f"  - {title}")
    if wayback_snippets:
        print(f"Wayback snippets: {len(wayback_snippets)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
