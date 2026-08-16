#!/usr/bin/env python3
"""
mirror-archive.py

Fetch indexed .rbs / .rbm files, SHA-256 them, and store content-addressed
blobs so duplicate paths collapse. Writes the canonical preservation record
``src/data/archive-integrity.json``.

The playable core (demo songs already in-repo) is hashed from disk and never
requires the network. Full-corpus fetches write under ``.mirror/`` (gitignored).

Usage:
    python3 scripts/mirror-archive.py --scope core
    python3 scripts/mirror-archive.py --scope all --kind rbs --sleep 0.15
    python3 scripts/mirror-archive.py --scope core --dry-run
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.parse import quote

import requests

REPO_ROOT = Path(__file__).resolve().parents[1]
SONGS_INDEX = REPO_ROOT / "src" / "data" / "songs-full-index.json"
MODS_INDEX = REPO_ROOT / "src" / "data" / "mods-full-index.json"
INTEGRITY_PATH = REPO_ROOT / "src" / "data" / "archive-integrity.json"
CORE_PUBLIC_PATH = REPO_ROOT / "public" / "data" / "archive-core.json"
MIRROR_ROOT = REPO_ROOT / ".mirror"
FAILURES_LOG = MIRROR_ROOT / "failures.jsonl"
DEMO_DIR = REPO_ROOT / "public" / "archive" / "rbs-songs" / "demo"
MODS_REMOTE_BASE = "https://storage.1ink.us/rebirth_mods"
USER_AGENT = "rebirth-website-mirror/1.0 (+https://github.com/ford442/rebirth_website)"
MAX_RBS_BYTES = 2 * 1024 * 1024
MAX_RBM_BYTES = 20 * 1024 * 1024

DEMO_LOCAL_BY_PATH: dict[str, str] = {
    "By_Source/Rebirth_2.0/Complete/A Taste of Haste.rbs": "a-taste-of-haste.rbs",
    "Monthly_Archive/FEBRUARY/Orbiting your Heart.rbs": "orbiting-your-heart.rbs",
    "Monthly_Archive/SEPTEMBER/Troublemakers.rbs": "troublemakers.rbs",
    "Artists/Cavey/3_acid.rbs": "cavey-3-acid.rbs",
    "Artists/Noah_Cohn_Complete/20.rbs": "noah-cohn-20.rbs",
    "Artists/DJ Knightmare/amnesya.rbs": "dj-knightmare-amnesya.rbs",
    "By_Source/Rebirth_2.0/Complete/Metallicon City.rbs": "metallicon-city.rbs",
    "By_Source/Rebirth_2.0/Complete/#008.rbs": "propellerhead-008.rbs",
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def blob_path_for(root: Path, digest: str) -> Path:
    return root / "blobs" / "sha256" / digest[:2] / digest[2:4] / digest


def write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name, dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)
            handle.write("\n")
        os.replace(tmp_name, path)
    except Exception:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def empty_integrity() -> dict[str, Any]:
    return {
        "meta": {
            "generated": utc_now(),
            "policy": "split",
            "adr": "docs/adr/0001-archive-hosting-split.md",
            "blobStore": ".mirror/blobs/sha256",
            "stats": {
                "paths": 0,
                "uniqueBlobs": 0,
                "mirrored": 0,
                "localCore": 0,
                "dead": 0,
                "remote": 0,
                "bytesUnique": 0,
            },
        },
        "entries": {},
    }


def seed_jobs(kind: str) -> list[dict[str, Any]]:
    jobs: list[dict[str, Any]] = []
    if kind in ("rbs", "all"):
        songs = load_json(SONGS_INDEX).get("songs") or []
        for song in songs:
            path = song.get("path")
            if not path:
                continue
            jobs.append(
                {
                    "path": path,
                    "kind": "rbs",
                    "url": song.get("url"),
                    "fileSize": song.get("fileSize"),
                    "filename": song.get("filename"),
                }
            )
    if kind in ("rbm", "all"):
        mods = load_json(MODS_INDEX).get("mods") or []
        for mod in mods:
            filename = mod.get("filename")
            if not filename:
                continue
            jobs.append(
                {
                    "path": f"rbm-mods/{filename}",
                    "kind": "rbm",
                    "url": f"{MODS_REMOTE_BASE}/{quote(filename)}",
                    "fileSize": mod.get("size"),
                    "filename": filename,
                }
            )
    return jobs


def in_scope(job: dict[str, Any], scope: str) -> bool:
    if scope == "all":
        return True
    if scope == "phase-a":
        path = job["path"]
        return (
            job["kind"] == "rbs"
            and (
                path.startswith("Artists/")
                or path.startswith("Featured_Collections/")
                or path in DEMO_LOCAL_BY_PATH
            )
        )
    # core: playable same-origin demos only (still seed other index rows as remote)
    return job["path"] in DEMO_LOCAL_BY_PATH


def record_failure(path: str, error: str) -> None:
    FAILURES_LOG.parent.mkdir(parents=True, exist_ok=True)
    with FAILURES_LOG.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps({"at": utc_now(), "path": path, "error": error}) + "\n")


def store_blob(data: bytes, digest: str) -> Path:
    dest = blob_path_for(MIRROR_ROOT, digest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not dest.is_file():
        tmp = dest.with_suffix(".part")
        tmp.write_bytes(data)
        os.replace(tmp, dest)
    return dest


def link_path_alias(archive_path: str, blob: Path) -> None:
    alias = MIRROR_ROOT / "paths" / archive_path
    alias.parent.mkdir(parents=True, exist_ok=True)
    if alias.exists() or alias.is_symlink():
        try:
            alias.unlink()
        except OSError:
            return
    try:
        os.link(blob, alias)
    except OSError:
        try:
            alias.symlink_to(blob.resolve())
        except OSError:
            alias.write_bytes(blob.read_bytes())


def fetch_bytes(session: requests.Session, url: str, max_bytes: int) -> tuple[bytes | None, str | None]:
    try:
        response = session.get(url, timeout=45, stream=True)
    except requests.RequestException as exc:
        return None, f"http_error:{exc}"
    if response.status_code == 404:
        return None, "http_404"
    if response.status_code != 200:
        return None, f"http_{response.status_code}"
    chunks: list[bytes] = []
    total = 0
    try:
        for chunk in response.iter_content(65536):
            if not chunk:
                continue
            total += len(chunk)
            if total > max_bytes:
                return None, "too_large"
            chunks.append(chunk)
    except requests.RequestException as exc:
        return None, f"http_error:{exc}"
    if total == 0:
        return None, "truncated"
    return b"".join(chunks), None


def hash_local_demo(archive_path: str) -> tuple[str, int, str] | tuple[None, None, str]:
    name = DEMO_LOCAL_BY_PATH.get(archive_path)
    if not name:
        return None, None, "not_core"
    file_path = DEMO_DIR / name
    if not file_path.is_file():
        return None, None, "missing_local_core"
    data = file_path.read_bytes()
    return sha256_bytes(data), len(data), f"archive/rbs-songs/demo/{name}"


def recount(payload: dict[str, Any]) -> None:
    entries: dict[str, Any] = payload.get("entries") or {}
    blobs: dict[str, int] = {}
    mirrored = local_core = dead = remote = 0
    for entry in entries.values():
        status = entry.get("status")
        digest = entry.get("sha256")
        nbytes = entry.get("bytes") or 0
        if digest:
            blobs[digest] = int(nbytes)
        if status == "local-core":
            local_core += 1
        elif status == "mirrored":
            mirrored += 1
        elif status == "dead":
            dead += 1
        else:
            remote += 1
    payload["meta"]["generated"] = utc_now()
    payload["meta"]["stats"] = {
        "paths": len(entries),
        "uniqueBlobs": len(blobs),
        "mirrored": mirrored,
        "localCore": local_core,
        "dead": dead,
        "remote": remote,
        "bytesUnique": sum(blobs.values()),
    }


def write_core_sidecar(payload: dict[str, Any]) -> None:
    core = []
    for path, entry in sorted((payload.get("entries") or {}).items()):
        if entry.get("status") != "local-core":
            continue
        core.append(
            {
                "path": path,
                "sha256": entry.get("sha256"),
                "bytes": entry.get("bytes"),
                "localPath": entry.get("localPath"),
            }
        )
    write_json_atomic(
        CORE_PUBLIC_PATH,
        {
            "meta": {
                "generated": utc_now(),
                "count": len(core),
                "policy": "split",
            },
            "songs": core,
        },
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Mirror and hash the ReBirth archive corpus.")
    parser.add_argument("--scope", choices=("core", "phase-a", "all"), default="core")
    parser.add_argument("--kind", choices=("rbs", "rbm", "all"), default="rbs")
    parser.add_argument("--sleep", type=float, default=0.15)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--retry-failed", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--output", type=Path, default=INTEGRITY_PATH)
    args = parser.parse_args()

    if not SONGS_INDEX.is_file():
        print(f"Missing {SONGS_INDEX}", file=sys.stderr)
        return 1

    payload = load_json(args.output) if args.output.is_file() else empty_integrity()
    payload.setdefault("entries", {})
    payload.setdefault("meta", empty_integrity()["meta"])
    payload["meta"]["policy"] = "split"
    payload["meta"]["adr"] = "docs/adr/0001-archive-hosting-split.md"

    jobs = seed_jobs(args.kind)
    # Always insert every indexed path so the record is complete.
    for job in jobs:
        payload["entries"].setdefault(
            job["path"],
            {
                "kind": job["kind"],
                "sha256": None,
                "bytes": None,
                "firstSeen": utc_now(),
                "sources": [job["url"]] if job.get("url") else [],
                "status": "remote",
                "localPath": None,
                "error": None,
                "fileSize": job.get("fileSize"),
            },
        )
        existing = payload["entries"][job["path"]]
        if job.get("url") and job["url"] not in (existing.get("sources") or []):
            existing.setdefault("sources", []).append(job["url"])

    fetch_jobs = [job for job in jobs if in_scope(job, args.scope)]
    if args.limit > 0:
        fetch_jobs = fetch_jobs[: args.limit]

    print(
        f"Integrity paths={len(payload['entries'])} fetch={len(fetch_jobs)} scope={args.scope}",
        file=sys.stderr,
    )
    if args.dry_run:
        for job in fetch_jobs[:30]:
            print(f"PLAN {job['kind']} {job['path']}", file=sys.stderr)
        return 0

    session = requests.Session()
    session.headers.update({"User-Agent": USER_AGENT})

    for job in fetch_jobs:
        path = job["path"]
        entry = payload["entries"][path]
        if (
            entry.get("sha256")
            and entry.get("status") in {"local-core", "mirrored"}
            and not args.retry_failed
        ):
            continue

        if path in DEMO_LOCAL_BY_PATH:
            digest, nbytes, local_path = hash_local_demo(path)
            if digest is None:
                entry["status"] = "dead"
                entry["error"] = local_path
                record_failure(path, str(local_path))
                print(f"FAIL {path} {local_path}", file=sys.stderr)
                continue
            entry.update(
                {
                    "sha256": digest,
                    "bytes": nbytes,
                    "status": "local-core",
                    "localPath": local_path,
                    "error": None,
                }
            )
            if not entry.get("firstSeen"):
                entry["firstSeen"] = utc_now()
            print(f"CORE {path} sha256:{digest[:12]}… {nbytes}B", file=sys.stderr)
            continue

        # Remote fetch (phase-a / all).
        max_bytes = MAX_RBM_BYTES if job["kind"] == "rbm" else MAX_RBS_BYTES
        data, error = fetch_bytes(session, job["url"], max_bytes)
        if args.sleep > 0:
            time.sleep(args.sleep)
        if error or data is None:
            entry["status"] = "dead" if error == "http_404" else "remote"
            entry["error"] = error
            record_failure(path, error or "unknown")
            print(f"FAIL {path} {error}", file=sys.stderr)
            continue
        digest = sha256_bytes(data)
        blob = store_blob(data, digest)
        link_path_alias(path, blob)
        entry.update(
            {
                "sha256": digest,
                "bytes": len(data),
                "status": "mirrored",
                "localPath": None,
                "error": None,
            }
        )
        if not entry.get("firstSeen"):
            entry["firstSeen"] = utc_now()
        print(f"OK {path} sha256:{digest[:12]}… {len(data)}B", file=sys.stderr)

    recount(payload)
    write_json_atomic(args.output, payload)
    write_core_sidecar(payload)
    stats = payload["meta"]["stats"]
    print(
        f"Wrote {args.output} paths={stats['paths']} unique={stats['uniqueBlobs']} "
        f"core={stats['localCore']} mirrored={stats['mirrored']} dead={stats['dead']} "
        f"bytes={stats['bytesUnique']}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
