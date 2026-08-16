#!/usr/bin/env python3
"""
build-song-discovery.py

Post-process songs-full-index.json into discovery assets:
  - public/data/songs-search-index.json  (compact records + facet tallies for MiniSearch)
  - src/data/artists-index.json          (canonical artist pages)

Merges binary sidecar metadata (scripts/enrich-rbs-catalog.py) and
human overrides (scripts/song-metadata-overrides.json).

Keep ARTIST_ALIASES in sync with src/data/artist-aliases.ts.

Run after index-rbs-archive.py (and optionally enrich-rbs-catalog.py).

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
BINARY_META_PATH = Path("src/data/songs-binary-meta.json")
OVERRIDES_PATH = Path("scripts/song-metadata-overrides.json")
INTEGRITY_PATH = Path("src/data/archive-integrity.json")

BPM_MIN = 20
BPM_MAX = 220

# GLOB "title" is usually the loaded *mod* name, not the song name.
GENERIC_TITLES = {
    "standard rebirth",
    "untitled",
    "untitled song",
    "orbit 2.0",
    "rebirth orbit 2.0",
    "minimod",
    "nord beat",
    "pitch black edition 2.0",
    "massen's msm 2.0",
    "massens msm 2.0",
    "regulation issue",
    "metallicon+",
    "metallicon",
    "axiom peace",
    "anomaly 2.0",
    "anokhav2",
    "omega",
    "poison-box",
    "ibex mkii",
    "ibex mk ii",
    "trancer",
    "ice",
    "massen redtop 2.0",
}


def is_usable_song_title(title: str, filename: str) -> bool:
    if not title or not title.strip():
        return False
    if is_filename_title(title, filename):
        return False
    normalized = " ".join(title.lower().split())
    if normalized in GENERIC_TITLES or normalized.startswith("untitled"):
        return False
    return True


def normalize_author(raw: str | None) -> str | None:
    if not raw:
        return None
    text = raw.strip()
    text = re.sub(r"^(by|©|\(c\))\s+", "", text, flags=re.I).strip()
    if not text:
        return None
    if re.search(r"©|\(c\)|copyright|\b19\d{2}\b|\b20\d{2}\b|'9\d|'0\d", text, re.I):
        return None
    if text[0] in "*{[":
        return None
    if len(text) > 48:
        return None
    return text

# Must stay in sync with src/data/artist-aliases.ts
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


def filename_stem_title(filename: str) -> str:
    stem = filename[:-4] if filename.lower().endswith(".rbs") else filename
    return stem.replace("_", " ").replace("-", " ").strip()


def is_filename_title(title: str, filename: str) -> bool:
    a = " ".join(title.lower().split())
    b = " ".join(filename_stem_title(filename).lower().split())
    return a == b


def load_binary_meta() -> dict[str, dict[str, Any]]:
    if not BINARY_META_PATH.exists():
        return {}
    payload = json.loads(BINARY_META_PATH.read_text(encoding="utf-8"))
    entries = payload.get("entries") or {}
    return entries if isinstance(entries, dict) else {}


def load_integrity() -> dict[str, dict[str, Any]]:
    if not INTEGRITY_PATH.exists():
        return {}
    payload = json.loads(INTEGRITY_PATH.read_text(encoding="utf-8"))
    entries = payload.get("entries") or {}
    return entries if isinstance(entries, dict) else {}


def load_overrides() -> dict[str, dict[str, Any]]:
    if not OVERRIDES_PATH.exists():
        return {}
    payload = json.loads(OVERRIDES_PATH.read_text(encoding="utf-8"))
    rows = payload.get("songs") or []
    return {row["path"]: row for row in rows if isinstance(row, dict) and row.get("path")}


def coerce_bpm(value: Any) -> int | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        bpm = int(round(float(value)))
    except (TypeError, ValueError):
        return None
    if BPM_MIN <= bpm <= BPM_MAX:
        return bpm
    return None


def merge_title(song: dict[str, Any], meta: dict[str, Any], override: dict[str, Any]) -> str:
    if override.get("title"):
        return str(override["title"]).strip()
    meta_title = (meta.get("metaTitle") or "").strip()
    if meta.get("parseOk") and is_usable_song_title(meta_title, song["filename"]):
        return meta_title
    return song.get("title") or filename_stem_title(song["filename"])


def merge_artist_label(song: dict[str, Any], meta: dict[str, Any], override: dict[str, Any]) -> str | None:
    if override.get("artist"):
        return str(override["artist"]).strip()
    path_artist = song.get("artist")
    if not path_artist and song.get("collection") == "Artists":
        path_artist = song.get("subcollection")
    if path_artist:
        return path_artist
    meta_author = normalize_author(meta.get("metaAuthor"))
    if meta.get("parseOk") and meta_author:
        return meta_author
    return None


def merge_bpm(song: dict[str, Any], meta: dict[str, Any], override: dict[str, Any]) -> int | None:
    if "bpm" in override and override["bpm"] is not None:
        return coerce_bpm(override["bpm"])
    if meta.get("parseOk"):
        parsed = coerce_bpm(meta.get("metaBpm"))
        if parsed is not None:
            return parsed
    return coerce_bpm(song.get("bpm"))


def enrich_song(
    song: dict[str, Any],
    song_id: int,
    meta: dict[str, Any],
    override: dict[str, Any],
    integrity: dict[str, Any],
) -> dict[str, Any]:
    artist_label = merge_artist_label(song, meta, override)
    artist_name, artist_slug = match_artist(artist_label)
    meta_author = normalize_author(meta.get("metaAuthor")) if meta.get("parseOk") else None
    glob_title = (meta.get("metaTitle") or "").strip() or None if meta.get("parseOk") else None
    link_status = integrity.get("status") or "remote"
    local_path = integrity.get("localPath")

    return {
        "id": song_id,
        "filename": song["filename"],
        "path": song["path"],
        "url": song["url"],
        "title": merge_title(song, meta, override),
        "collection": song.get("collection"),
        "subcollection": song.get("subcollection"),
        "subcollectionDisplay": display_subcollection(song.get("subcollection")),
        "artistCanonical": artist_name,
        "artistSlug": artist_slug,
        "metaAuthor": meta_author,
        "modName": glob_title,
        "bpm": merge_bpm(song, meta, override),
        "fileSize": song.get("fileSize"),
        "sourceVersion": infer_source_version(song["path"]),
        "hasModDependency": infer_mod_dependency(song["path"]),
        "localPath": local_path,
        "linkStatus": link_status,
        "sha256": integrity.get("sha256"),
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
    binary_meta = load_binary_meta()
    overrides = load_overrides()
    integrity = load_integrity()
    enriched = [
        enrich_song(
            s,
            i,
            binary_meta.get(s.get("path"), {}),
            overrides.get(s.get("path"), {}),
            integrity.get(s.get("path"), {}),
        )
        for i, s in enumerate(raw_songs)
    ]

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
