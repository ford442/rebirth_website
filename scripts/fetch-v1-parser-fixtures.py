#!/usr/bin/env python3
"""Materialize v1 / v1.5 parser fixtures.

Preferred path: decode the committed *.b64 payloads next to each fixture.
Fallback: download official SongPacks from the Internet Archive.

    python3 scripts/fetch-v1-parser-fixtures.py
"""

from __future__ import annotations

import base64
import hashlib
import io
import json
import zipfile
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "src" / "wasm" / "test-fixtures"
MANIFEST = FIXTURES / "manifest.json"

PACKS = [
    "https://archive.org/download/rebirth-rb338-songpacks/RBSongPack%2001-10.zip",
    "https://archive.org/download/rebirth-rb338-songpacks/RBSongPack%2011-20.zip",
    "https://archive.org/download/rebirth-rb338-songpacks/RBSongPack%2031-39%20%2B%20Cherry%20Coke%20TOP25.zip",
]

WANTED = {
    "RBSongPack 09/Just 15.RBS": "v1/just-15.rbs",
    "RBSongPack 02/Retrograde.RBS": "v1/retrograde.rbs",
    "RBSongPack 37/Gurkensalat.RBS": "v15/gurkensalat.rbs",
    "RBSongPack 18/Hermetico Absoluto.RBS": "v15/hermetico-absoluto.rbs",
}

B64_TARGETS = [
    "v1/just-15.rbs",
    "v1/retrograde.rbs",
    "v15/gurkensalat.rbs",
    "v15/hermetico-absoluto.rbs",
    "v15/cavey-3-acid.rbs",
    "reject/generic-smf.mid",
]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def expected_hashes() -> dict[str, str]:
    if not MANIFEST.is_file():
        return {}
    rows = json.loads(MANIFEST.read_text())
    return {row["path"].split("test-fixtures/", 1)[-1]: row["sha256"] for row in rows}


def write_checked(rel: str, data: bytes, expected: dict[str, str]) -> None:
    out = FIXTURES / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    digest = sha256(data)
    want = expected.get(rel)
    if want and digest != want:
        raise SystemExit(f"checksum mismatch for {rel}: {digest} != {want}")
    out.write_bytes(data)
    print(f"wrote {out.relative_to(ROOT)} ({len(data)} bytes, {digest})")


def decode_committed_b64(expected: dict[str, str]) -> list[str]:
    missing = []
    for rel in B64_TARGETS:
        payload = FIXTURES / f"{rel}.b64"
        if not payload.is_file():
            missing.append(rel)
            continue
        data = base64.b64decode(payload.read_text().encode("ascii"), validate=True)
        write_checked(rel, data, expected)
    return missing


def download_missing(missing: list[str], expected: dict[str, str]) -> None:
    still = [rel for rel in missing if rel in WANTED.values()]
    if not still:
        return
    invert = {v: k for k, v in WANTED.items()}
    remaining = {invert[rel]: rel for rel in still}
    for url in PACKS:
        if not remaining:
            break
        print(f"GET {url}")
        with urlopen(url, timeout=120) as resp:
            blob = resp.read()
        with zipfile.ZipFile(io.BytesIO(blob)) as zf:
            names = set(zf.namelist())
            for inner, dest in list(remaining.items()):
                if inner not in names:
                    continue
                data = zf.read(inner)
                if not data.startswith(b"MThd") or b"Propellerhead Software" not in data[:240]:
                    raise SystemExit(f"{inner} is not a Propellerhead v1/v1.5 song")
                write_checked(dest, data, expected)
                del remaining[inner]
    if remaining:
        raise SystemExit("missing from packs: " + ", ".join(remaining))


def main() -> int:
    expected = expected_hashes()
    missing = decode_committed_b64(expected)
    if missing:
        print("b64 missing for:", ", ".join(missing))
        download_missing(missing, expected)
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
