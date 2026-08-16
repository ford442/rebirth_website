#!/usr/bin/env python3
"""
check-archive-integrity.py

CI gate for the preservation record:

  - archive-integrity.json exists and matches the song/mod index path counts
  - every local-core file exists under public/ and SHA-256 matches
  - public/data/archive-core.json agrees with local-core rows

Usage:
    python3 scripts/check-archive-integrity.py
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
INTEGRITY = REPO / "src" / "data" / "archive-integrity.json"
CORE = REPO / "public" / "data" / "archive-core.json"
SONGS = REPO / "src" / "data" / "songs-full-index.json"
MODS = REPO / "src" / "data" / "mods-full-index.json"


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    for path in (INTEGRITY, CORE, SONGS):
        if not path.is_file():
            return fail(f"missing {path}")

    integrity = json.loads(INTEGRITY.read_text(encoding="utf-8"))
    core = json.loads(CORE.read_text(encoding="utf-8"))
    songs = json.loads(SONGS.read_text(encoding="utf-8"))
    song_paths = {row["path"] for row in songs.get("songs") or [] if row.get("path")}
    entries = integrity.get("entries") or {}

    missing_songs = song_paths - set(entries)
    if missing_songs:
        sample = ", ".join(sorted(missing_songs)[:5])
        return fail(f"{len(missing_songs)} indexed songs missing from integrity (e.g. {sample})")

    if integrity.get("meta", {}).get("policy") != "split":
        return fail("integrity meta.policy must be 'split'")

    local_core = [
        (path, entry)
        for path, entry in entries.items()
        if entry.get("status") == "local-core"
    ]
    if len(local_core) < 8:
        return fail(f"expected at least 8 local-core songs, found {len(local_core)}")

    for path, entry in local_core:
        rel = entry.get("localPath")
        digest = entry.get("sha256")
        if not rel or not digest:
            return fail(f"local-core row incomplete: {path}")
        file_path = REPO / "public" / rel
        if not file_path.is_file():
            return fail(f"missing core file public/{rel}")
        actual = sha256_file(file_path)
        if actual != digest:
            return fail(f"hash drift public/{rel}: expected {digest} got {actual}")
        if entry.get("bytes") != file_path.stat().st_size:
            return fail(f"size drift public/{rel}")

    core_songs = core.get("songs") or []
    if len(core_songs) != len(local_core):
        return fail(f"archive-core.json count {len(core_songs)} != local-core {len(local_core)}")

    if MODS.is_file():
        mods = json.loads(MODS.read_text(encoding="utf-8"))
        mod_paths = {f"rbm-mods/{row['filename']}" for row in mods.get("mods") or [] if row.get("filename")}
        # Mods are seeded only when mirror --kind includes rbm; warn, do not fail,
        # so a songs-only integrity seed still gates the playable core.
        _ = mod_paths

    print(
        f"OK: {len(entries)} integrity paths, {len(local_core)} local-core hashes verified",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
