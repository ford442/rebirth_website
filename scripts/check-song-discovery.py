#!/usr/bin/env python3
"""
check-song-discovery.py

CI sanity check for the song catalog pipeline outputs.

  - public/data/songs-search-index.json song count matches the full index
  - required MiniSearch record keys are present
  - committed sidecar parseOk rows have at least one metadata field
  - if the sidecar has any entries, parseOk must not be entirely empty

Usage:
    python3 scripts/check-song-discovery.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

FULL_INDEX_PATH = Path("src/data/songs-full-index.json")
SEARCH_INDEX_PATH = Path("public/data/songs-search-index.json")
ARTISTS_INDEX_PATH = Path("src/data/artists-index.json")
BINARY_META_PATH = Path("src/data/songs-binary-meta.json")

REQUIRED_SONG_KEYS = {
    "id",
    "filename",
    "path",
    "url",
    "title",
    "collection",
    "artistCanonical",
    "artistSlug",
    "bpm",
    "fileSize",
}


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def main() -> int:
    for path in (FULL_INDEX_PATH, SEARCH_INDEX_PATH, ARTISTS_INDEX_PATH):
        if not path.is_file():
            return fail(f"missing {path}")

    full = json.loads(FULL_INDEX_PATH.read_text(encoding="utf-8"))
    search = json.loads(SEARCH_INDEX_PATH.read_text(encoding="utf-8"))
    artists = json.loads(ARTISTS_INDEX_PATH.read_text(encoding="utf-8"))

    full_total = full.get("meta", {}).get("totalFiles")
    if full_total is None:
        full_total = len(full.get("songs") or [])
    search_songs = search.get("songs") or []
    if len(search_songs) != full_total:
        return fail(f"search songs {len(search_songs)} != full index {full_total}")

    if search.get("meta", {}).get("totalSongs") != len(search_songs):
        return fail("search meta.totalSongs does not match songs array length")

    for i, song in enumerate(search_songs[:25]):
        missing = REQUIRED_SONG_KEYS - set(song)
        if missing:
            return fail(f"search song[{i}] missing keys: {sorted(missing)}")

    artist_rows = artists.get("artists") or []
    if artists.get("meta", {}).get("artistCount") != len(artist_rows):
        return fail("artists meta.artistCount does not match artists array length")

    if BINARY_META_PATH.is_file():
        sidecar = json.loads(BINARY_META_PATH.read_text(encoding="utf-8"))
        entries: dict[str, Any] = sidecar.get("entries") or {}
        if entries:
            ok_rows = [e for e in entries.values() if e.get("parseOk") is True]
            if not ok_rows:
                return fail("sidecar has entries but parseOk rate is 0")
            for path, entry in entries.items():
                if not entry.get("parseOk"):
                    continue
                if not any(entry.get(k) not in (None, "") for k in ("metaTitle", "metaAuthor", "metaBpm")):
                    return fail(f"parseOk sidecar row missing metadata: {path}")

    print(
        f"OK: {len(search_songs)} songs, {len(artist_rows)} artists, "
        f"sidecar={BINARY_META_PATH.is_file()}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
