#!/usr/bin/env python3
"""Generate a large synthetic `.rbm` that fills most of the 8 MiB SamplePool.

Used by the WASM heap probe so CI can measure live+mod+bounce without
vendoring Metallicon.rbm (3.4M). Not committed — regenerate with:

    python3 scripts/make-rbm-large-kit-fixture.py
"""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "src/wasm/test-fixtures/mods/large-kit.rbm"

SAMPLE_RATE = 44100
COPYRIGHT = b"(c)1998 Propellerhead Software, all rights reserved."
# 1.8M float32 frames ≈ 6.9 MiB of the 8 MiB arena (leave headroom for 303 tables).
ARENA_FRAMES = 1_800_000


def wav_silence_with_clicks(frames: int, sample_rate: int = SAMPLE_RATE) -> bytes:
    """Mono 16-bit WAV: mostly zeros, a click every second so decode isn't empty."""
    pcm = bytearray(frames * 2)
    click = struct.pack("<h", 12000)
    for n in range(0, frames, sample_rate):
        pcm[n * 2 : n * 2 + 2] = click
    fmt = struct.pack("<HHIIHH", 1, 1, sample_rate, sample_rate * 2, 2, 16)
    fmt_chunk = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    data_chunk = b"data" + struct.pack("<I", len(pcm)) + bytes(pcm)
    body = b"WAVE" + fmt_chunk + data_chunk
    return b"RIFF" + struct.pack("<I", len(body)) + body


def chunk(chunk_id: bytes, body: bytes) -> bytes:
    out = chunk_id + struct.pack(">I", len(body)) + body
    if len(body) % 2:
        out += b"\x00"
    return out


def embf(name: str, payload: bytes) -> bytes:
    return chunk(b"EMBF", name.encode("ascii") + b"\x00" + payload)


def build() -> bytes:
    head = bytes([0x5B, 0x35, 0xB3, 0x5B, 0xBC, 0x01, 0x00, 0x00]) + COPYRIGHT + b"\x00"
    head = head.ljust(256, b"\x00")

    info = b"Large Kit Heap Probe\x00"
    info += b"Synthetic arena-filling mod for WASM heap measurement.\x00"
    info = info.ljust(1280, b"\x00")

    body = b"PRBM"
    body += chunk(b"HEAD", head)
    body += embf("tr808bd.wav", wav_silence_with_clicks(ARENA_FRAMES))
    body += chunk(b"INFO", info)
    return b"CAT " + struct.pack(">I", len(body)) + body


def main() -> None:
    data = build()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    rel = OUT_PATH.relative_to(REPO_ROOT)
    print(f"wrote {rel} ({len(data)} bytes)")
    print(f"sha256: {digest}")


if __name__ == "__main__":
    main()
