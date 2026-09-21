#!/usr/bin/env python3
"""
extract-rbm-skins.py

Extract the JPEG panel-art resources embedded in `.rbm` mod files and mirror
a small selection of them into `public/archive/rbm-skins/` for the mods
browser thumbnail/lightbox, plus a "skin the player" CSS background.

Shells out to the native `rbm-inspect --extract-skins` CLI (built from the
same RbmParser the WASM engine uses), which writes each embedded Skin-kind
resource's raw bytes to a directory. This script downloads the source
`.rbm`, runs that extraction, picks a bounded number of the largest images
per mod (icons are tiny; the panel backgrounds/splash screens are the ones
worth showing), copies them into `public/`, and records them in
`src/data/archive-integrity.json` (same ledger the song/mod mirror uses) and
a small `src/data/rbm-skins-index.json` lookup the Astro pages consume at
build time.

KNOWN LIMITATION — RbmParser reads each EMBF resource's payload as the raw
bytes the chunk header declares (name-terminator to declared chunk end), the
same way it reads `.aif` sample payloads. For AIFF that is enough, because
the format carries its own internal chunk sizes. Corpus testing here found
that is *not* enough for the larger skin JPEGs: small icon-sized resources
(roughly under ~1KB) usually decode cleanly, but every full-size splash/panel
image sampled across a dozen real mods decoded only a few pixel rows before
the entropy-coded scan data went bad — a symptom of the resource being more
than a flat byte range (segmented/interleaved storage from its Mac resource-
fork origins is the leading theory, unconfirmed). Rather than ship
placeholder-gray "art", this script validates every candidate with a real
JPEG decode (`--require-pillow`, on by default) and only ever writes/records
images that pass. Until the underlying RbmParser resource-boundary handling
is fixed, expect this to reject most large images and populate few or no
mods — that is the pipeline working as designed, not a bug in this script.
Contributions reverse-engineering the true EMBF payload layout are welcome.

Usage:
    python3 scripts/extract-rbm-skins.py --defaults
    python3 scripts/extract-rbm-skins.py --filenames Metallicon.rbm,Orbit2.rbm
    python3 scripts/extract-rbm-skins.py --all --limit 20 --max-per-mod 2

Requires: requests, and Pillow for the validation decode (pip install Pillow).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import requests

try:
    from PIL import Image, ImageFile

    # Never silently accept a truncated/corrupt decode as "valid" — that is
    # exactly the class of bug this validation exists to catch.
    ImageFile.LOAD_TRUNCATED_IMAGES = False
    HAVE_PILLOW = True
except ImportError:
    HAVE_PILLOW = False

REPO_ROOT = Path(__file__).resolve().parents[1]
MODS_FULL_INDEX = REPO_ROOT / "src" / "data" / "mods-full-index.json"
SKINS_INDEX_PATH = REPO_ROOT / "src" / "data" / "rbm-skins-index.json"
INTEGRITY_PATH = REPO_ROOT / "src" / "data" / "archive-integrity.json"
CORE_PUBLIC_PATH = REPO_ROOT / "public" / "data" / "archive-core.json"
OUT_ROOT = REPO_ROOT / "public" / "archive" / "rbm-skins"
DEFAULT_INSPECT = REPO_ROOT / "src" / "wasm" / "cpp" / "build" / "rbm-inspect"
MODS_REMOTE_BASE = "https://storage.1ink.us/rebirth_mods"
USER_AGENT = "rebirth-website-skin-extractor/1.0 (+https://github.com/ford442/rebirth_website)"
MAX_RBM_BYTES = 20 * 1024 * 1024

# The four official defaults from the ReBirth 2.0.1 CD-ROM, plus a few other
# mods this site already features by name (history.astro, index.astro) — a
# small, safe starting set. Run with --all to backfill the rest of the 367.
DEFAULT_FILENAMES = [
    "Metallicon.rbm",
    "Orbit2.rbm",
    "PBE2.rbm",
    "MSM2.rbm",
    "MiDiMoD.rbm",
    "redstripe-dnb.rbm",
    "nordbeat.rbm",
]

SKIN_LINE_RE = re.compile(r"^SKIN (.+) (\d+)$")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def load_json(path: Path, default: Any) -> Any:
    if not path.is_file():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json_atomic(path: Path, payload: Any) -> None:
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


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def slugify_stem(filename: str) -> str:
    stem = filename[:-4] if filename.lower().endswith(".rbm") else filename
    slug = re.sub(r"[^a-zA-Z0-9._-]+", "-", stem).strip("-").lower()
    return slug or "mod"


def download_mod(session: requests.Session, filename: str) -> bytes | None:
    url = f"{MODS_REMOTE_BASE}/{filename}"
    try:
        resp = session.get(url, timeout=30, stream=True)
        resp.raise_for_status()
        chunks: list[bytes] = []
        total = 0
        for chunk in resp.iter_content(chunk_size=65536):
            total += len(chunk)
            if total > MAX_RBM_BYTES:
                print(f"  skip {filename}: exceeds {MAX_RBM_BYTES} bytes", file=sys.stderr)
                return None
            chunks.append(chunk)
        return b"".join(chunks)
    except requests.RequestException as exc:
        print(f"  download failed for {filename}: {exc}", file=sys.stderr)
        return None


def extract_skins(inspect_bin: Path, rbm_path: Path, out_dir: Path) -> list[tuple[str, int]]:
    result = subprocess.run(
        [str(inspect_bin), "--extract-skins", str(out_dir), str(rbm_path)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if result.returncode != 0:
        print(f"  rbm-inspect failed: {result.stderr.strip()}", file=sys.stderr)
        return []
    found: list[tuple[str, int]] = []
    for line in result.stdout.splitlines():
        m = SKIN_LINE_RE.match(line)
        if m:
            found.append((m.group(1), int(m.group(2))))
    return found


# The panel-art resources this pipeline is after (splash screens, background
# stills) run roughly square around 256x256 in every mod sampled while
# corpus-testing this script. Small icon/label/slider fragments (as small as
# 15x9) decode validly far more often than full panel art currently does
# (see the module docstring), but a 15x9 sliver stretched into a card
# thumbnail is worse than showing nothing — so a minimum footprint is part
# of "valid", not just a decodable byte stream.
MIN_SKIN_DIMENSION = 96
MIN_SKIN_PIXELS = MIN_SKIN_DIMENSION * MIN_SKIN_DIMENSION


def is_valid_skin_image(path: Path) -> bool:
    """Full decode, not just a header sniff — see the module docstring: a
    corrupt EMBF resource typically still has a valid-looking SOI/SOF header
    and only breaks partway through the entropy-coded scan data, which a
    magic-bytes check alone would miss. Also rejects anything too small to
    read as panel art rather than a UI icon fragment."""
    try:
        with Image.open(path) as im:
            im.load()
            w, h = im.size
    except Exception:
        return False
    return w >= MIN_SKIN_DIMENSION and h >= MIN_SKIN_DIMENSION and w * h >= MIN_SKIN_PIXELS


def process_mod(
    session: requests.Session,
    inspect_bin: Path,
    filename: str,
    max_per_mod: int,
    dry_run: bool,
) -> dict[str, Any] | None:
    print(f"{filename} …")
    data = download_mod(session, filename)
    if data is None:
        return None

    slug = slugify_stem(filename)
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir)
        rbm_path = tmp_path / filename
        rbm_path.write_bytes(data)
        extract_dir = tmp_path / "skins"
        found = extract_skins(inspect_bin, rbm_path, extract_dir)
        if not found:
            print("  no skin resources found")
            return None

        # Splash/title screens are the useful panel art; the rest are tiny
        # button/knob icons. Prefer anything named "splash", then fall back
        # to the largest images (icons run a few KB; panel art runs 10s-100s).
        def rank(item: tuple[str, int]) -> tuple[int, int]:
            name, size = item
            is_splash = 1 if "splash" in name.lower() else 0
            return (is_splash, size)

        ranked = sorted(found, key=rank, reverse=True)

        # Validate in rank order and keep the first `max_per_mod` that
        # actually decode — do not fall back to writing an unvalidated
        # candidate just to hit the count.
        selected: list[tuple[str, int]] = []
        rejected = 0
        for name, size in ranked:
            if len(selected) >= max_per_mod:
                break
            candidate = extract_dir / name
            if not candidate.is_file():
                continue
            if is_valid_skin_image(candidate):
                selected.append((name, size))
            else:
                rejected += 1

        if rejected:
            print(f"  rejected {rejected} corrupt/undecodable resource(s)")
        if not selected:
            print("  no valid skin image survived decode validation — skipping mod")
            return None

        if dry_run:
            print(f"  would extract {len(selected)}/{len(found)} skins: {[n for n, _ in selected]}")
            return None

        out_dir = OUT_ROOT / slug
        out_dir.mkdir(parents=True, exist_ok=True)
        skins: list[dict[str, Any]] = []
        for name, _size in selected:
            src = extract_dir / name
            content = src.read_bytes()
            dest = out_dir / name
            dest.write_bytes(content)
            skins.append(
                {
                    "file": name,
                    "bytes": len(content),
                    "sha256": sha256_bytes(content),
                }
            )

    if not skins:
        return None

    primary = next((s["file"] for s in skins if "splash" in s["file"].lower()), skins[0]["file"])
    print(f"  extracted {len(skins)} validated skin(s), primary={primary}")
    return {"filename": filename, "slug": slug, "primary": primary, "skins": skins}


def recount_integrity(integrity: dict[str, Any]) -> None:
    """Mirror mirror-archive.py's recount() so stats stay accurate after we
    add entries outside that script's own run."""
    entries: dict[str, Any] = integrity.get("entries") or {}
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
    integrity.setdefault("meta", {})["stats"] = {
        "paths": len(entries),
        "uniqueBlobs": len(blobs),
        "mirrored": mirrored,
        "localCore": local_core,
        "dead": dead,
        "remote": remote,
        "bytesUnique": sum(blobs.values()),
    }


def write_core_sidecar(integrity: dict[str, Any]) -> None:
    """Mirror mirror-archive.py's write_core_sidecar() — `npm run
    archive:check` gates on every local-core row (any kind, not just songs)
    appearing here, so a run that only touches archive-integrity.json
    without refreshing this sidecar fails that check."""
    core = []
    for path, entry in sorted((integrity.get("entries") or {}).items()):
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
            "meta": {"generated": utc_now(), "count": len(core), "policy": "split"},
            "songs": core,
        },
    )


def update_integrity(integrity: dict[str, Any], mod_filename: str, mod_result: dict[str, Any]) -> None:
    entries: dict[str, Any] = integrity.setdefault("entries", {})
    source_url = f"{MODS_REMOTE_BASE}/{mod_filename}"
    for skin in mod_result["skins"]:
        path = f"rbm-skins/{mod_result['slug']}/{skin['file']}"
        local_path = f"archive/rbm-skins/{mod_result['slug']}/{skin['file']}"
        entries[path] = {
            "kind": "rbm-skin",
            "sha256": skin["sha256"],
            "bytes": skin["bytes"],
            "firstSeen": entries.get(path, {}).get("firstSeen", utc_now()),
            "sources": [source_url],
            "status": "local-core",
            "localPath": local_path,
            "error": None,
            "fileSize": None,
        }


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract .rbm skin (panel art) resources.")
    parser.add_argument("--inspect", type=Path, default=DEFAULT_INSPECT)
    parser.add_argument("--defaults", action="store_true", help="Use the curated featured-mod list.")
    parser.add_argument("--filenames", type=str, help="Comma-separated .rbm filenames to process.")
    parser.add_argument("--all", action="store_true", help="Process every mod in mods-full-index.json.")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--max-per-mod", type=int, default=3)
    parser.add_argument("--sleep", type=float, default=0.2)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if not HAVE_PILLOW:
        print(
            "Missing Pillow — required to validate extracted images actually decode "
            "(see the module docstring's KNOWN LIMITATION). Install with: pip install Pillow",
            file=sys.stderr,
        )
        return 1

    if not args.inspect.is_file():
        print(
            f"Missing rbm-inspect at {args.inspect}. "
            "Build with: cmake --build src/wasm/cpp/build --target rbm-inspect",
            file=sys.stderr,
        )
        return 1

    if args.filenames:
        targets = [f.strip() for f in args.filenames.split(",") if f.strip()]
    elif args.all:
        full_index = load_json(MODS_FULL_INDEX, {"mods": []})
        targets = [m["filename"] for m in full_index.get("mods", [])]
    else:
        targets = list(DEFAULT_FILENAMES)

    if args.limit > 0:
        targets = targets[: args.limit]

    skins_index = load_json(SKINS_INDEX_PATH, {"meta": {}, "mods": {}})
    skins_index.setdefault("mods", {})
    integrity = load_json(INTEGRITY_PATH, {"meta": {}, "entries": {}})

    processed = 0
    session = requests.Session()
    session.headers.update({"User-Agent": USER_AGENT})
    for i, filename in enumerate(targets):
        result = process_mod(session, args.inspect, filename, args.max_per_mod, args.dry_run)
        if result:
            skins_index["mods"][filename] = {
                "slug": result["slug"],
                "primary": result["primary"],
                "skins": result["skins"],
            }
            if not args.dry_run:
                update_integrity(integrity, filename, result)
            processed += 1
        if i < len(targets) - 1:
            time.sleep(args.sleep)

    if not args.dry_run:
        skins_index["meta"] = {
            "generated": utc_now(),
            "count": len(skins_index["mods"]),
        }
        write_json_atomic(SKINS_INDEX_PATH, skins_index)
        integrity.setdefault("meta", {})["generated"] = utc_now()
        recount_integrity(integrity)
        write_json_atomic(INTEGRITY_PATH, integrity)
        write_core_sidecar(integrity)

    print(f"\nProcessed {processed}/{len(targets)} mod(s) with skins extracted.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
