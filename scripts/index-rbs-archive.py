#!/usr/bin/env python3
"""
index-rbs-archive.py

Recursively crawl the remote RBS song archive at test.1ink.us and generate
``src/data/songs-full-index.json``. The generated index contains one entry per
``.rbs`` file with a direct download URL, file size, inferred collection/artist
metadata, and coverage statistics against ``public/rbs-manifest.json``.

Usage:
    python3 scripts/index-rbs-archive.py
    python3 scripts/index-rbs-archive.py --base-url http://test.1ink.us/rb338/archive/rbs-songs --output src/data/songs-full-index.json
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import sys
import urllib.parse
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import requests

DEFAULT_BASE_URL = "https://test.1ink.us/rb338/archive/rbs-songs"
DEFAULT_OUTPUT = "src/data/songs-full-index.json"
MANIFEST_PATH = "public/rbs-manifest.json"

# Apache directory-listing line parser.
# Matches: <a href="...">Name</a>    YYYY-MM-DD HH:MM    SIZE
LISTING_RE = re.compile(
    r'<a href="([^"]+)">([^<]+)</a>\s+(?:\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2})?\s*([\d\.\-KMG]*)',
    re.IGNORECASE,
)

# Optional BPM token such as "140bpm", "140 bpm", "140BPM"
BPM_RE = re.compile(r"(\d{2,3})\s?bpm", re.IGNORECASE)

# Monthly folder typos on the remote archive (path segment → display label).
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


def display_name(segment: str) -> str:
    """Turn a path segment like 'By_Source' or 'JANRUARY' into a display label."""
    if segment in MONTHLY_FOLDER_DISPLAY:
        return MONTHLY_FOLDER_DISPLAY[segment]
    upper = segment.upper()
    if upper in MONTHLY_FOLDER_DISPLAY:
        return MONTHLY_FOLDER_DISPLAY[upper]
    return segment.replace("_", " ").strip()


def parse_bpm(filename: str) -> int | None:
    """Return an integer BPM if the filename contains a clear BPM token."""
    match = BPM_RE.search(filename)
    if match:
        bpm = int(match.group(1))
        # ReBirth songs are rarely outside 60-220 BPM.
        if 60 <= bpm <= 220:
            return bpm
    return None


def fetch_listing(session: requests.Session, url: str) -> str:
    """Fetch a directory listing, raising a clear error on failure."""
    response = session.get(url, timeout=30)
    response.raise_for_status()
    return response.text


def crawl(
    session: requests.Session,
    base_url: str,
    path: str,
    files: list[dict[str, Any]],
    seen: set[str],
) -> None:
    """Recursively crawl directories starting at ``path`` (empty = root)."""
    if path in seen:
        return
    seen.add(path)

    url = base_url if not path else f"{base_url}/{path}"
    if not url.endswith("/"):
        url += "/"

    listing = fetch_listing(session, url)

    for line in listing.splitlines():
        match = LISTING_RE.search(line)
        if not match:
            continue

        href_raw, name_raw, size = match.groups()
        name = html.unescape(name_raw).rstrip("/")

        if "Parent Directory" in name or href_raw.startswith("?"):
            continue

        href = html.unescape(href_raw)
        is_dir = href.endswith("/")

        # Resolve the archive-relative path for this entry.
        if href.startswith("/"):
            rel = href[len("/rb338/archive/rbs-songs/") :].rstrip("/")
        else:
            decoded = urllib.parse.unquote(href).rstrip("/")
            rel = decoded if not path else f"{path}/{decoded}"

        if is_dir:
            crawl(session, base_url, rel, files, seen)
        elif href.lower().endswith(".rbs"):
            files.append(
                {
                    "filename": name,
                    "path": rel,
                    "size": (size or "-").strip(),
                }
            )


def build_entry(file_info: dict[str, Any], base_url: str) -> dict[str, Any]:
    """Enrich a raw file record with collection/artist metadata and URL."""
    path = file_info["path"]
    filename = file_info["filename"]
    segments = path.split("/")

    top_level = segments[0] if segments else "Unknown"
    sub_level = segments[1] if len(segments) > 1 else None

    collection = display_name(top_level)
    subcollection = display_name(sub_level) if sub_level else None

    artist: str | None = None
    if collection == "Artists" and sub_level:
        artist = display_name(sub_level)

    return {
        "filename": filename,
        "path": path,
        "url": f"{base_url}/{urllib.parse.quote(path, safe='/')}",
        "title": filename.replace(".rbs", "").replace("_", " ").replace("-", " ").strip(),
        "collection": collection,
        "subcollection": subcollection,
        "artist": artist,
        "bpm": parse_bpm(filename),
        "fileSize": file_info["size"],
        "source": base_url,
    }


def load_manifest_total(manifest_path: str) -> int | None:
    """Read totalSongs from the manifest, if available."""
    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
        return manifest.get("archive", {}).get("totalSongs")
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a full index of the remote RBS song archive."
    )
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help=f"Root archive URL to crawl (default: {DEFAULT_BASE_URL})",
    )
    parser.add_argument(
        "--output",
        default=DEFAULT_OUTPUT,
        help=f"Output JSON path (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--manifest",
        default=MANIFEST_PATH,
        help=f"Path to rbs-manifest.json for coverage stats (default: {MANIFEST_PATH})",
    )
    args = parser.parse_args()

    base_url = args.base_url.rstrip("/")
    output_path = Path(args.output)

    print(f"Crawling {base_url} ...", file=sys.stderr)

    session = requests.Session()
    session.headers.update(
        {
            "User-Agent": (
                "rebirth-website-indexer/1.0 "
                "(+https://github.com/ford442/rebirth_website)"
            )
        }
    )

    raw_files: list[dict[str, Any]] = []
    seen: set[str] = set()
    crawl(session, base_url, "", raw_files, seen)

    if not raw_files:
        print("No .rbs files found. Check the archive URL.", file=sys.stderr)
        return 1

    raw_files.sort(key=lambda f: f["filename"].lower())
    songs = [build_entry(f, base_url) for f in raw_files]

    total_files = len(songs)
    manifest_total = load_manifest_total(args.manifest)
    coverage_percent = None
    if manifest_total:
        coverage_percent = round((total_files / manifest_total) * 100, 1)

    collection_counts = Counter(song["collection"] for song in songs)

    output = {
        "songs": songs,
        "meta": {
            "generated": datetime.now(timezone.utc).isoformat(),
            "source": base_url,
            "totalFiles": total_files,
            "manifestTotal": manifest_total,
            "coveragePercent": coverage_percent,
            "collections": dict(collection_counts.most_common()),
        },
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2)

    print(f"Indexed {total_files} .rbs files -> {output_path}", file=sys.stderr)
    if coverage_percent is not None:
        print(
            f"Coverage: {coverage_percent}% of manifest estimate ({manifest_total})",
            file=sys.stderr,
        )
    print(
        "Collections: "
        + ", ".join(f"{k}={v}" for k, v in collection_counts.most_common()),
        file=sys.stderr,
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())
