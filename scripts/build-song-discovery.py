#!/usr/bin/env python3
"""
build-song-discovery.py

Post-process songs-full-index.json into discovery assets:
  - src/data/songs-search-index.json  (compact records + facet tallies for MiniSearch)
  - src/data/artists-index.json       (canonical artist pages)

Run after index-rbs-archive.py or whenever the full index changes.

Usage:
    python3 scripts/build-song-discovery.py
"""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

FULL_INDEX_PATH = Path("src/data/songs-full-index.json")
SEARCH_INDEX_PATH = Path("public/data/songs-search-index.json")
ARTISTS_INDEX_PATH = Path("src/data/artists-index.json")

ARTIST_ALIASES: list[dict[str, Any]] = [
    {
        "slug": "noah-cohn",
        "name": "Noah Cohn",
        "aliases": [
            "Noah Cohn",
            "Noah Cohn Complete",
            "Noah Cohn Extended",
            "Noah_Cohn_Complete",
            "Noah_Cohn_Extended",
        ],
    },
    {"slug": "rotorkopf", "name": "Rotorkopf", "aliases": ["Rotorkopf", "Rotorkopf 2"]},
    {"slug": "cavey", "name": "Cavey", "aliases": ["Cavey"]},
    {"slug": "dj-knightmare", "name": "DJ Knightmare", "aliases": ["DJ Knightmare"]},
    {"slug": "dj-caspa", "name": "DJ Caspa", "aliases": ["DJ Caspa"]},
    {"slug": "fanatic", "name": "Fanatic", "aliases": ["Fanatic"]},
]

MONTHLY_FOLDER_DISPLAY: dict[str, str] = {
    "JANRUARY": "January",
    "FEBRUARY": "February",
    "MARCH": "March",
    "APRIL": "April",
    "MAY": "May",
    "JUNE": "June",
    "JULY": "July",
    "AUGUST": "August",
    "SEPTEMBER": "September",
    "OCTOBER": "October",
    "OCTOBER 2": "October (vol. 2)",
    "NOVEMBER": "November",
    "DECEMBER": "December",
}

ALIAS_LOOKUP: dict[str, tuple[str, str]] = {}
for entry in ARTIST_ALIASES:
    for alias in entry["aliases"]:
        ALIAS_LOOKUP[alias.lower()] = (entry["name"], entry["slug"])


def slugify(name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return slug or "artist"


def display_subcollection(raw: str | None) -> str | None:
    if not raw:
        return None
    return MONTHLY_FOLDER_DISPLAY.get(raw, MONTHLY_FOLDER_DISPLAY.get(raw.upper(), raw.replace("_", " ")))


def match_artist(label: str | None) -> tuple[str | None, str | None]:
    if not label:
        return None, None
    key = label.strip().lower()
    if key in ALIAS_LOOKUP:
        return ALIAS_LOOKUP[key]
    name = label.strip()
    return name, slugify(name)


def infer_source_version(path: str) -> str | None:
    match = re.search(r"By_Source/Rebirth_(\d+(?:\.\d+)?)", path, re.I)
    if match:
        return match.group(1)
    if "By_Source/Archives" in path:
        return "archive"
    if path.startswith("Monthly_Archive/"):
        return "monthly"
    return None


def infer_mod_dependency(path: str) -> bool:
    return "Featured_Collections/MODs" in path or "/MODs/" in path


def enrich_song(song: dict[str, Any], song_id: int) -> dict[str, Any]:
    artist_label = song.get("artist")
    if not artist_label and song.get("collection") == "Artists":
        artist_label = song.get("subcollection")
    artist_name, artist_slug = match_artist(artist_label)

    return {
        "id": song_id,
        "filename": song["filename"],
        "path": song["path"],
        "url": song["url"],
        "title": song.get("title") or song["filename"].replace(".rbs", ""),
        "collection": song.get("collection"),
        "subcollection": song.get("subcollection"),
        "subcollectionDisplay": display_subcollection(song.get("subcollection")),
        "artistCanonical": artist_name,
        "artistSlug": artist_slug,
        "bpm": song.get("bpm"),
        "fileSize": song.get("fileSize"),
        "sourceVersion": infer_source_version(song["path"]),
        "hasModDependency": infer_mod_dependency(song["path"]),
    }


def build_artists_index(songs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: dict[str, dict[str, Any]] = {
        entry["slug"]: {
            "slug": entry["slug"],
            "name": entry["name"],
            "aliases": list(entry["aliases"]),
            "songCount": 0,
            "folders": [],
        }
        for entry in ARTIST_ALIASES
    }

    for song in songs:
        slug = song.get("artistSlug")
        name = song.get("artistCanonical")
        if not slug or not name:
            continue
        row = rows.setdefault(
            slug,
            {"slug": slug, "name": name, "aliases": [name], "songCount": 0, "folders": []},
        )
        row["songCount"] += 1
        folder = "/".join(song["path"].split("/")[:-1])
        if folder and folder not in row["folders"]:
            row["folders"].append(folder)

    return sorted(
        [r for r in rows.values() if r["songCount"] > 0],
        key=lambda r: (-r["songCount"], r["name"].lower()),
    )


def main() -> int:
    if not FULL_INDEX_PATH.exists():
        print(f"Missing {FULL_INDEX_PATH}. Run scripts/index-rbs-archive.py first.", file=sys.stderr)
        return 1

    full = json.loads(FULL_INDEX_PATH.read_text(encoding="utf-8"))
    raw_songs = full.get("songs", [])
    enriched = [enrich_song(s, i) for i, s in enumerate(raw_songs)]

    collection_counts = Counter(s["collection"] for s in enriched if s.get("collection"))
    source_counts = Counter(s["sourceVersion"] for s in enriched if s.get("sourceVersion"))
    mod_count = sum(1 for s in enriched if s.get("hasModDependency"))
    bpm_values = [s["bpm"] for s in enriched if isinstance(s.get("bpm"), int)]

    search_payload = {
        "meta": {
            "generated": datetime.now(timezone.utc).isoformat(),
            "totalSongs": len(enriched),
            "source": str(FULL_INDEX_PATH),
        },
        "facets": {
            "collections": [
                {"name": name, "count": count}
                for name, count in collection_counts.most_common()
            ],
            "sourceVersions": [
                {"name": name, "count": count}
                for name, count in source_counts.most_common()
            ],
            "modDependentCount": mod_count,
            "bpmRange": {
                "min": min(bpm_values) if bpm_values else None,
                "max": max(bpm_values) if bpm_values else None,
                "withBpm": len(bpm_values),
            },
        },
        "songs": enriched,
    }

    artists_payload = {
        "meta": {
            "generated": datetime.now(timezone.utc).isoformat(),
            "artistCount": 0,
        },
        "artists": build_artists_index(enriched),
    }
    artists_payload["meta"]["artistCount"] = len(artists_payload["artists"])

    SEARCH_INDEX_PATH.parent.mkdir(parents=True, exist_ok=True)
    SEARCH_INDEX_PATH.write_text(json.dumps(search_payload, indent=2), encoding="utf-8")
    ARTISTS_INDEX_PATH.write_text(json.dumps(artists_payload, indent=2), encoding="utf-8")

    print(f"Wrote {len(enriched)} songs -> {SEARCH_INDEX_PATH}")
    print(f"Wrote {artists_payload['meta']['artistCount']} artists -> {ARTISTS_INDEX_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
