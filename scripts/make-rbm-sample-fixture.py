#!/usr/bin/env python3
"""Generate the synthetic `.rbm` sample fixture used by the native tests.

`minimal.rbm` deliberately carries a 12-byte `FORM....AIFF` stub with no COMM
or SSND, which is exactly what you want for testing that a malformed payload
degrades cleanly — but it has no PCM, so it cannot exercise the decode ->
SamplePool -> SampleVoice path at all.

This script writes `sample-kit.rbm`: a container with real, tiny audio in
both of the formats ModPacker produced, plus a skin that must never reach the
audio thread.

    python3 scripts/make-rbm-sample-fixture.py

Regenerate whenever the fixture's contents need to change, then update the
sha256 in src/wasm/test-fixtures/manifest.json and the row in FIXTURES.md.
"""

from __future__ import annotations

import hashlib
import math
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "src/wasm/test-fixtures/mods/sample-kit.rbm"

SAMPLE_RATE = 44100
COPYRIGHT = b"(c)1998 Propellerhead Software, all rights reserved."


# ── PCM generators (deterministic; no randomness, so the fixture is stable) ──


def decaying_sine(frames: int, freq: float, decay: float) -> list[int]:
    """Kick-ish: a sine that decays exponentially. Returns int16 samples."""
    out = []
    for n in range(frames):
        t = n / SAMPLE_RATE
        amplitude = math.exp(-decay * t)
        value = math.sin(2.0 * math.pi * freq * t) * amplitude
        out.append(max(-32768, min(32767, int(value * 30000))))
    return out


def noise_burst(frames: int, decay: float) -> list[int]:
    """Snare-ish: a deterministic LCG 'noise' burst under an exponential decay."""
    out = []
    state = 0x13579BDF
    for n in range(frames):
        state = (state * 1103515245 + 12345) & 0x7FFFFFFF
        sample = ((state >> 8) & 0xFFFF) - 32768
        amplitude = math.exp(-decay * (n / SAMPLE_RATE))
        out.append(max(-32768, min(32767, int(sample * amplitude * 0.8))))
    return out


def single_cycle_saw(frames: int) -> list[int]:
    """One cycle of a saw, for the 303 wavetable slot."""
    return [int((-1.0 + 2.0 * (n / frames)) * 30000) for n in range(frames)]


# ── Container writers ────────────────────────────────────────────────────────


def ieee80(value: float) -> bytes:
    """Encode a sample rate as the 80-bit extended float AIFF's COMM uses."""
    if value <= 0:
        return b"\x00" * 10
    exponent = 0
    mantissa = value
    while mantissa < 0x8000000000000000:
        mantissa *= 2
        exponent -= 1
        if exponent < -16382:
            break
    exponent += 16383 + 63
    # Re-derive the mantissa at the settled exponent to avoid drift.
    mantissa = int(value * (2.0 ** (63 - (exponent - 16383))))
    return struct.pack(">H", exponent) + struct.pack(">Q", mantissa)


def aiff(samples: list[int], sample_rate: int = SAMPLE_RATE) -> bytes:
    """Minimal FORM/AIFF, mono, 16-bit big-endian PCM."""
    pcm = b"".join(struct.pack(">h", s) for s in samples)
    comm = struct.pack(">hIh", 1, len(samples), 16) + ieee80(float(sample_rate))
    comm_chunk = b"COMM" + struct.pack(">I", len(comm)) + comm
    ssnd_body = struct.pack(">II", 0, 0) + pcm
    ssnd_chunk = b"SSND" + struct.pack(">I", len(ssnd_body)) + ssnd_body
    if len(ssnd_body) % 2:
        ssnd_chunk += b"\x00"
    body = b"AIFF" + comm_chunk + ssnd_chunk
    return b"FORM" + struct.pack(">I", len(body)) + body


def wav(samples: list[int], sample_rate: int = SAMPLE_RATE) -> bytes:
    """Minimal RIFF/WAVE, mono, 16-bit little-endian PCM."""
    pcm = b"".join(struct.pack("<h", s) for s in samples)
    fmt = struct.pack("<HHIIHH", 1, 1, sample_rate, sample_rate * 2, 2, 16)
    fmt_chunk = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    data_chunk = b"data" + struct.pack("<I", len(pcm)) + pcm
    if len(pcm) % 2:
        data_chunk += b"\x00"
    body = b"WAVE" + fmt_chunk + data_chunk
    return b"RIFF" + struct.pack("<I", len(body)) + body


def fake_jpeg() -> bytes:
    """Just enough JPEG magic to be classified as a skin. Never decoded."""
    return b"\xff\xd8\xff\xe0" + b"\x00\x10JFIF\x00" + b"\x00" * 32 + b"\xff\xd9"


def chunk(chunk_id: bytes, body: bytes) -> bytes:
    """Propellerhead chunk: 4-byte id, big-endian size, body, pad to even."""
    out = chunk_id + struct.pack(">I", len(body)) + body
    if len(body) % 2:
        out += b"\x00"
    return out


def embf(name: str, payload: bytes) -> bytes:
    """EMBF: NUL-terminated filename immediately followed by the raw file."""
    return chunk(b"EMBF", name.encode("ascii") + b"\x00" + payload)


def build() -> bytes:
    head = bytes([0x5B, 0x35, 0xB3, 0x5B, 0xBC, 0x01, 0x00, 0x00]) + COPYRIGHT + b"\x00"
    head = head.ljust(256, b"\x00")

    info = b"Sample Kit Test Mod\x00"
    info += b"Synthetic mod with real AIFF/WAV payloads for decoder tests.\x00"
    info = info.ljust(1280, b"\x00")

    body = b"PRBM"
    body += chunk(b"HEAD", head)
    # 808 kick: AIFF, the format ModPacker actually wrote.
    body += embf("tr808bd.aif", aiff(decaying_sine(2048, 62.0, 14.0)))
    # 909 snare: WAV, the rare-but-real case handled by dr_wav.
    body += embf("tr909sd.wav", wav(noise_burst(1536, 26.0)))
    # 303 oscillator replacement: a single-cycle wavetable.
    body += embf("303saw.aif", aiff(single_cycle_saw(128)))
    # A skin, which must be catalogued but never decoded.
    body += embf("12522.jpg", fake_jpeg())
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
