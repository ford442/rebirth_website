#!/usr/bin/env python3
"""
enrich-rbs-catalog.py

Download (or open a local mirror of) .rbs files listed in
``src/data/songs-full-index.json`` and inspect them with native
``rbs-inspect``. Write a resumable sidecar cache of binary metadata
(title, author, BPM) to ``src/data/songs-binary-meta.json``.

Does not rewrite the crawl index. Run ``build-song-discovery.py`` after
this script to merge sidecar + overrides into the search/artist indexes.

Usage:
    python3 scripts/enrich-rbs-catalog.py --phase A
    python3 scripts/enrich-rbs-catalog.py --phase A --local-mirror public/archive/rbs-songs
    python3 scripts/enrich-rbs-catalog.py --phase B --limit 50 --retry-failed
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import requests

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INDEX = REPO_ROOT / "src" / "data" / "songs-full-index.json"
DEFAULT_CACHE = REPO_ROOT / "src" / "data" / "songs-binary-meta.json"
DEFAULT_INSPECT = REPO_ROOT / "src" / "wasm" / "cpp" / "build" / "rbs-inspect"
DEMO_DIR = REPO_ROOT / "public" / "archive" / "rbs-songs" / "demo"
MAX_BYTES = 2 * 1024 * 1024
FLUSH_EVERY = 10
USER_AGENT = "rebirth-website-enricher/1.0 (+https://github.com/ford442/rebirth_website)"

# Keep in sync with scripts/sync-demo-songs.py (archivePath → local demo file).
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


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


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


def filename_stem_title(filename: str) -> str:
    stem = filename[:-4] if filename.lower().endswith(".rbs") else filename
    return stem.replace("_", " ").replace("-", " ").strip()


def is_filename_title(meta_title: str, filename: str) -> bool:
    a = " ".join(meta_title.lower().split())
    b = " ".join(filename_stem_title(filename).lower().split())
    return a == b


def in_phase(song: dict[str, Any], phase: str) -> bool:
    path = song.get("path") or ""
    if phase.upper() == "B":
        return True
    if path.startswith("Artists/") or path.startswith("Featured_Collections/"):
        return True
    if path in DEMO_LOCAL_BY_PATH:
        return True
    return False


def resolve_local(song: dict[str, Any], mirror: Path | None) -> Path | None:
    path = song["path"]
    demo_name = DEMO_LOCAL_BY_PATH.get(path)
    if demo_name:
        demo_file = DEMO_DIR / demo_name
        if demo_file.is_file():
            return demo_file
    if mirror:
        candidate = mirror / path
        if candidate.is_file():
            return candidate
        # Demo-style layout: files stored under demo/ by kebab name only.
        flat = mirror / Path(path).name
        if flat.is_file():
            return flat
    return None


def cache_hit(entry: dict[str, Any] | None, file_size: str, retry_failed: bool) -> bool:
    if not entry:
        return False
    if entry.get("fileSize") != file_size:
        return False
    if "parseOk" not in entry:
        return False
    if retry_failed and entry.get("parseOk") is False:
        return False
    return True


def decode_proc_text(raw: bytes | None) -> str:
    if not raw:
        return ""
    return raw.decode("utf-8", errors="replace")


def inspect_file(inspect_bin: Path, rbs_path: Path) -> tuple[dict[str, Any] | None, str | None]:
    try:
        result = subprocess.run(
            [str(inspect_bin), str(rbs_path)],
            capture_output=True,
            timeout=30,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return None, f"inspect_error:{exc}"

    stdout = decode_proc_text(result.stdout)
    stderr = decode_proc_text(result.stderr)

    if result.returncode != 0:
        err = (stderr or stdout).strip().splitlines()
        detail = err[-1] if err else f"exit_{result.returncode}"
        return None, f"inspect_nonzero:{detail[:240]}"

    try:
        return json.loads(stdout), None
    except json.JSONDecodeError as exc:
        return None, f"inspect_invalid_json:{exc}"


def download_to_temp(session: requests.Session, url: str) -> tuple[Path | None, str | None]:
    try:
        response = session.get(url, timeout=45, stream=True)
    except requests.RequestException as exc:
        return None, f"http_error:{exc}"

    if response.status_code == 404:
        return None, "http_404"
    if response.status_code != 200:
        return None, f"http_{response.status_code}"

    tmp = tempfile.NamedTemporaryFile(prefix="rbs-enrich-", suffix=".rbs", delete=False)
    written = 0
    try:
        for chunk in response.iter_content(chunk_size=65536):
            if not chunk:
                continue
            written += len(chunk)
            if written > MAX_BYTES:
                tmp.close()
                os.unlink(tmp.name)
                return None, "too_large"
            tmp.write(chunk)
        tmp.close()
        if written == 0:
            os.unlink(tmp.name)
            return None, "truncated"
        return Path(tmp.name), None
    except requests.RequestException as exc:
        tmp.close()
        try:
            os.unlink(tmp.name)
        except OSError:
            pass
        return None, f"http_error:{exc}"


def empty_sidecar(inspect_bin: Path, phase: str) -> dict[str, Any]:
    return {
        "meta": {
            "generated": utc_now(),
            "inspectBinary": str(inspect_bin),
            "sourceIndex": str(DEFAULT_INDEX.relative_to(REPO_ROOT)),
            "phase": phase,
            "stats": {"ok": 0, "failed": 0, "skippedCache": 0, "pending": 0},
        },
        "entries": {},
    }


def recount_stats(sidecar: dict[str, Any], pending: int) -> None:
    entries = sidecar.get("entries") or {}
    ok = sum(1 for e in entries.values() if e.get("parseOk") is True)
    failed = sum(1 for e in entries.values() if e.get("parseOk") is False)
    sidecar["meta"]["stats"] = {
        "ok": ok,
        "failed": failed,
        "skippedCache": sidecar["meta"].get("stats", {}).get("skippedCache", 0),
        "pending": pending,
    }
    sidecar["meta"]["generated"] = utc_now()


def main() -> int:
    parser = argparse.ArgumentParser(description="Enrich the RBS catalog from binary metadata.")
    parser.add_argument("--phase", choices=("A", "B"), default="A")
    parser.add_argument("--index", type=Path, default=DEFAULT_INDEX)
    parser.add_argument("--output", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--inspect", type=Path, default=DEFAULT_INSPECT)
    parser.add_argument("--local-mirror", type=Path, default=None)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--sleep", type=float, default=0.15)
    parser.add_argument("--retry-failed", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    inspect_bin = args.inspect
    if not inspect_bin.is_file():
        print(
            f"Missing rbs-inspect at {inspect_bin}. "
            "Build with: cmake --build src/wasm/cpp/build --target rbs-inspect",
            file=sys.stderr,
        )
        return 1

    if not args.index.is_file():
        print(f"Missing {args.index}. Run scripts/index-rbs-archive.py first.", file=sys.stderr)
        return 1

    full = load_json(args.index)
    songs = [s for s in full.get("songs", []) if in_phase(s, args.phase)]
    songs.sort(key=lambda s: s.get("path") or "")

    sidecar = load_json(args.output) if args.output.is_file() else empty_sidecar(inspect_bin, args.phase)
    if "entries" not in sidecar:
        sidecar["entries"] = {}
    sidecar.setdefault("meta", empty_sidecar(inspect_bin, args.phase)["meta"])
    sidecar["meta"]["phase"] = args.phase
    sidecar["meta"]["inspectBinary"] = str(inspect_bin)

    planned: list[dict[str, Any]] = []
    skipped = 0
    for song in songs:
        path = song["path"]
        size = song.get("fileSize") or ""
        existing = sidecar["entries"].get(path)
        if cache_hit(existing, size, args.retry_failed):
            skipped += 1
            continue
        planned.append(song)

    if args.limit > 0:
        planned = planned[: args.limit]

    print(
        f"Phase {args.phase}: {len(songs)} in scope, {skipped} cached, {len(planned)} to inspect",
        file=sys.stderr,
    )

    if args.dry_run:
        for song in planned[:50]:
            print(f"PLAN {song['path']}", file=sys.stderr)
        if len(planned) > 50:
            print(f"... {len(planned) - 50} more", file=sys.stderr)
        return 0

    sidecar["meta"]["stats"]["skippedCache"] = skipped
    session = requests.Session()
    session.headers.update({"User-Agent": USER_AGENT})

    title_improved = 0
    parse_ok = 0
    inspected = 0

    try:
        for song in planned:
            path = song["path"]
            size = song.get("fileSize") or ""
            filename = song.get("filename") or Path(path).name
            local = resolve_local(song, args.local_mirror)
            tmp_path: Path | None = None
            source_path = local
            error: str | None = None

            if source_path is None:
                tmp_path, error = download_to_temp(session, song["url"])
                source_path = tmp_path
                if args.sleep > 0:
                    time.sleep(args.sleep)

            entry: dict[str, Any] = {
                "fileSize": size,
                "parseOk": False,
                "parseError": error,
                "metaTitle": None,
                "metaAuthor": None,
                "metaBpm": None,
                "inspectedAt": utc_now(),
            }

            if source_path is not None and error is None:
                try:
                    parsed, inspect_err = inspect_file(inspect_bin, source_path)
                except Exception as exc:  # noqa: BLE001 — never abort the batch
                    parsed, inspect_err = None, f"inspect_error:{exc}"
                if inspect_err:
                    entry["parseError"] = inspect_err
                elif parsed is not None:
                    title = (parsed.get("title") or "").strip() or None
                    author = (parsed.get("author") or "").strip() or None
                    bpm_raw = parsed.get("bpm")
                    bpm_val: float | None
                    try:
                        bpm_val = float(bpm_raw) if bpm_raw is not None else None
                    except (TypeError, ValueError):
                        bpm_val = None
                    entry["parseOk"] = True
                    entry["parseError"] = None
                    entry["metaTitle"] = title
                    entry["metaAuthor"] = author
                    entry["metaBpm"] = bpm_val
                    parse_ok += 1
                    if title and not is_filename_title(title, filename):
                        title_improved += 1

            if tmp_path is not None:
                try:
                    tmp_path.unlink()
                except OSError:
                    pass

            sidecar["entries"][path] = entry
            inspected += 1
            status = "OK" if entry["parseOk"] else "FAIL"
            reason = entry["parseError"] or (entry["metaTitle"] or "")
            print(f"{status} {path} {reason}", file=sys.stderr)

            if inspected % FLUSH_EVERY == 0:
                recount_stats(sidecar, pending=len(planned) - inspected)
                write_json_atomic(args.output, sidecar)
    finally:
        recount_stats(sidecar, pending=max(0, len(planned) - inspected))
        write_json_atomic(args.output, sidecar)

    stats = sidecar["meta"]["stats"]
    print(
        f"Wrote {args.output} ok={stats['ok']} failed={stats['failed']} "
        f"cached={stats['skippedCache']} pending={stats['pending']}",
        file=sys.stderr,
    )
    if parse_ok:
        rate = title_improved / parse_ok
        print(
            f"This run: titleImproved={title_improved}/{parse_ok} ({rate:.1%})",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
