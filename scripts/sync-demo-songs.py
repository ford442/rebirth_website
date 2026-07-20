#!/usr/bin/env python3
"""
sync-demo-songs.py

Download curated .rbs demo files from the remote archive into
``public/archive/rbs-songs/demo/``, compute SHA-256 checksums, and
update the ``demos`` block in ``public/rbs-manifest.json``.

Catalog entries must stay aligned with ``src/data/demo-songs.ts``.

Usage:
    python3 scripts/sync-demo-songs.py
    python3 scripts/sync-demo-songs.py --dry-run
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.parse
from pathlib import Path
from typing import Any

import requests

REPO_ROOT = Path(__file__).resolve().parents[1]
DEMO_DIR = REPO_ROOT / "public" / "archive" / "rbs-songs" / "demo"
MANIFEST_PATH = REPO_ROOT / "public" / "rbs-manifest.json"
ARCHIVE_BASE = "https://test.1ink.us/rb338/archive/rbs-songs"

LICENSING_NOTE = (
    "ReBirth RB-338 is discontinued abandonware (Propellerhead Software / Reason Studios). "
    "These demo files are mirrored from the community archive for non-commercial preservation "
    "and in-browser playback on this site. Original authors retain their rights; contact us "
    "via a GitHub issue for takedown requests."
)

# Keep in sync with src/data/demo-songs.ts
DEMO_CATALOG: list[dict[str, Any]] = [
    {
        "id": "a-taste-of-haste",
        "label": "Action Jackson — A Taste of Haste",
        "filename": "a-taste-of-haste.rbs",
        "archivePath": "By_Source/Rebirth_2.0/Complete/A Taste of Haste.rbs",
        "category": "official",
    },
    {
        "id": "orbiting-your-heart",
        "label": "ElekD — Orbiting your Heart",
        "filename": "orbiting-your-heart.rbs",
        "archivePath": "Monthly_Archive/FEBRUARY/Orbiting your Heart.rbs",
        "category": "official",
    },
    {
        "id": "troublemakers",
        "label": "Micromat — Troublemakers",
        "filename": "troublemakers.rbs",
        "archivePath": "Monthly_Archive/SEPTEMBER/Troublemakers.rbs",
        "category": "official",
    },
    {
        "id": "cavey-3-acid",
        "label": "Cavey — 3 acid",
        "filename": "cavey-3-acid.rbs",
        "archivePath": "Artists/Cavey/3_acid.rbs",
        "category": "community",
        "bpm": 128,
    },
    {
        "id": "noah-cohn-20",
        "label": "Noah Cohn — 20",
        "filename": "noah-cohn-20.rbs",
        "archivePath": "Artists/Noah_Cohn_Complete/20.rbs",
        "category": "community",
        "bpm": 132,
    },
    {
        "id": "dj-knightmare-amnesya",
        "label": "DJ Knightmare — amnesya",
        "filename": "dj-knightmare-amnesya.rbs",
        "archivePath": "Artists/DJ Knightmare/amnesya.rbs",
        "category": "community",
        "bpm": 134,
    },
    {
        "id": "metallicon-city",
        "label": "T.G.ViRUS — Metallicon City",
        "filename": "metallicon-city.rbs",
        "archivePath": "By_Source/Rebirth_2.0/Complete/Metallicon City.rbs",
        "category": "community",
    },
    {
        "id": "propellerhead-008",
        "label": "Propellerhead — #008",
        "filename": "propellerhead-008.rbs",
        "archivePath": "By_Source/Rebirth_2.0/Complete/#008.rbs",
        "category": "official",
    },
]


def archive_download_url(relative_path: str) -> str:
    encoded = "/".join(urllib.parse.quote(segment, safe="") for segment in relative_path.split("/"))
    return f"{ARCHIVE_BASE}/{encoded}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sync_demo(entry: dict[str, Any], dry_run: bool) -> dict[str, Any]:
    url = archive_download_url(entry["archivePath"])
    dest = DEMO_DIR / entry["filename"]
    print(f"  {entry['filename']} <- {url}")

    if dry_run:
        return {
            "filename": entry["filename"],
            "label": entry["label"],
            "archivePath": entry["archivePath"],
            "category": entry["category"],
            "sha256": "<dry-run>",
            "bytes": 0,
        }

    response = requests.get(url, timeout=120)
    response.raise_for_status()
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(response.content)
    file_hash = sha256_file(dest)
    size = dest.stat().st_size
    print(f"    OK {size:,} bytes  sha256={file_hash[:16]}…")
    manifest_entry: dict[str, Any] = {
        "filename": entry["filename"],
        "label": entry["label"],
        "archivePath": entry["archivePath"],
        "category": entry["category"],
        "sha256": file_hash,
        "bytes": size,
    }
    if "bpm" in entry:
        manifest_entry["bpm"] = entry["bpm"]
    return manifest_entry


def update_manifest(files: list[dict[str, Any]], dry_run: bool) -> None:
    manifest: dict[str, Any] = {}
    if MANIFEST_PATH.exists():
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))

    manifest["demos"] = {
        "localPath": "archive/rbs-songs/demo",
        "licensingNote": LICENSING_NOTE,
        "files": files,
    }

    if dry_run:
        print(json.dumps(manifest["demos"], indent=2))
        return

    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Updated {MANIFEST_PATH.relative_to(REPO_ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync local RBS demo files from remote archive")
    parser.add_argument("--dry-run", action="store_true", help="Print actions without writing files")
    args = parser.parse_args()

    print(f"Syncing {len(DEMO_CATALOG)} demo files to {DEMO_DIR.relative_to(REPO_ROOT)}/")
    files: list[dict[str, Any]] = []
    for entry in DEMO_CATALOG:
        try:
            files.append(sync_demo(entry, args.dry_run))
        except requests.RequestException as exc:
            print(f"    FAILED: {exc}", file=sys.stderr)
            return 1

    update_manifest(files, args.dry_run)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
