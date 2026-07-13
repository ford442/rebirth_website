# ReBirth RB-338 `.rbs` File Format Specification

> **Status:** Community reverse-engineered — based on hex-dump analysis of
> real archive `.rbs` files and the [ReBirth Museum](https://www.rebirthmuseum.com/) archive.
> This document describes the **Propellerhead chunk container** used by the
> v2.x files in the community archive, not the older plain-header/offset draft.
> Corrections and additions are welcome.

---

## 1. Overview

An `.rbs` file stores a complete ReBirth RB-338 song. The on-disk layout is a
nested Propellerhead-style chunk container (`CAT ` / `RB40`) rather than a flat
header + offset table.

Each file encodes:

- Song metadata (title, author, info text, creator URL)
- Initial state of all four devices (2× TB-303, TR-808, TR-909)
- 32 patterns per device (8 per bank × 4 banks A–D)
- The song arrangement — an ordered list of which pattern each device plays per bar
- Optional real-time automation / knob movements

File sizes typically range from **2 KB** (empty song) to **50 KB** (complex
arrangement with automation).

### Device order

Within the `DEVL` device-list container, chunks appear in this order:

1. `303 ` — TB-303 A
2. `303 ` — TB-303 B (second occurrence)
3. `808 ` — TR-808
4. `909 ` — TR-909

v1.5 files (not yet observed in the archive) are expected to omit the second
303 and the 909.

---

## 2. Root Container

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 4 | Chunk ID | `"CAT "` (0x43 0x41 0x54 0x20) |
| 0x04 | 4 | Chunk Size | Big-endian uint32 — size of the container body that follows |
| 0x08 | 4 | Format Marker | `"RB40"` (0x52 0x42 0x34 0x30) |

The body after `"RB40"` is a sequence of top-level chunks. Each chunk has a
4-byte ID, a 4-byte big-endian size, then `size` bytes of data. If `size` is
odd, a single padding byte is appended so the next chunk starts on an even
offset.

### Chunk header

```
+--------+--------+
| 4-byte ID       |
+--------+--------+
| 4-byte BE size  |
+--------+--------+
| size bytes data |
+--------+--------+
| [padding if odd]|
+--------+--------+
```

Top-level chunks observed in v2.x files:

| ID | Purpose |
|----|---------|
| `HEAD` | File/version header |
| `GLOB` | Global song settings + title |
| `USRI` | Author / info text |
| `CAT ` | Nested container (device list `DEVL`, track list `TRKL`) |

Unknown chunks should be skipped.

---

## 3. HEAD Chunk

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 4 | Signature | `"[T[T"` (0x5b 0x54 0x5b 0x54) |
| 0x04 | … | Header data | Version-specific fields |
| 0x06 | 1 | Sub-format marker | `0x01` or `0x02` in observed v2.x files |

The byte at offset `0x06` inside the HEAD chunk data is the main version
marker seen in the wild:

| Value | Mapped version |
|-------|----------------|
| `0x01` | v2.0 |
| `0x02` | v2.0.1 |

The rest of the 256-byte HEAD chunk is currently reserved / unused by the
parser.

---

## 4. GLOB Chunk — Global Settings & Title

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | ? | Unknown |
| 0x01 | 1 | ShowInfoOnOpen | Non-zero when the info text should be shown on load |
| 0x02 | 1 | ? | Unknown |
| 0x03 | 1 | Sub-format | `0x01` / `0x02` mirroring HEAD marker |
| 0x04–0x0e | 11 | ? | Unknown / padding |
| 0x0f | … | Title | Null-terminated string — song title |
| 0x119 | … | Creator URL | Null-terminated string (if present) |

The title is read as a C string starting at offset `0x0f` and trimmed of
whitespace. The creator URL is read from offset `0x119` when the chunk is large
enough.

**BPM / tempo:** The exact tempo field has not yet been confidently identified.
The parser currently leaves `bpm` at the default `125.0`.

---

## 5. USRI Chunk — Author & Info Text

The USRI chunk contains two consecutive null-terminated Latin-1 / Windows-1252
strings:

1. **Author** — starts at offset `0`
2. **Info text** — starts at the first non-zero byte after the author null

Both strings should be converted to UTF-8 for display. The info text is the
paragraph shown to the user when `ShowInfoOnOpen` is true.

---

## 6. Device State & Patterns (`DEVL` Container)

Device data lives inside a nested `CAT ` container whose marker is `DEVL`.
Inside `DEVL` the chunks are:

| ID | Purpose |
|----|---------|
| `MIXR` | Mixer levels, mutes, pan, FX sends for all four devices |
| `DELY` | Delay settings |
| `PCF ` | PCF settings |
| `DIST` | Distortion settings |
| `COMP` | Compressor settings |
| `303 ` | TB-303 state + 32 patterns (×2) |
| `808 ` | TR-808 state + 32 patterns |
| `909 ` | TR-909 state + 32 patterns |

### 6a. MIXR Chunk

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | Master level | 0–127 |
| 0x01–0x0f | 15 | ? | Unknown / padding |
| 0x10 | 12 | Device 0 mixer record | 303-A |
| 0x1c | 12 | Device 1 mixer record | 303-B |
| 0x28 | 12 | Device 2 mixer record | 808 |
| 0x34 | 12 | Device 3 mixer record | 909 |

Each 12-byte mixer record:

| Offset | Field |
|--------|-------|
| 0 | Mute (0 = muted) |
| 1 | Level 0–127 |
| 2 | Pan 0–127 (64 = centre) |
| 3 | ? |
| 4 | Distortion send (0/1) |
| 5 | PCF send (0/1) |
| 6 | Compressor send (0/1) |
| 7–11 | ? |

### 6b. TB-303 Device Chunk (`303 `)

Total chunk size observed: **1097 bytes**.

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | Tune | 0–127 |
| 0x01 | 1 | Cutoff | 0–127 |
| 0x02 | 1 | Resonance | 0–127 |
| 0x03 | 1 | EnvMod | 0–127 |
| 0x04 | 1 | Decay | 0–127 |
| 0x05 | 1 | Accent | 0–127 |
| 0x06 | 1 | Waveform | 0 = saw, 1 = square |
| 0x07 | 1 | Initial pattern bank | 0–3 |
| 0x08 | 1 | Initial pattern index | 0–7 |
| 0x09 | 1 | ? | Unknown |
| 0x0a–0x08 | ? | State padding | To offset 9 |
| 0x09 | 32 × 34 bytes | Pattern slots | See below |

Header size: **9 bytes**.
Pattern slot size: **34 bytes**.

### 6c. TR-808 Device Chunk (`808 `)

Total chunk size observed: **6238 bytes**.

Header size: **30 bytes**.
Pattern slot size: **194 bytes**.

The first 30 bytes contain the drum-kit parameter state. The mixer fields for
the 808 are taken from the `MIXR` chunk, not from this device chunk.

### 6d. TR-909 Device Chunk (`909 `)

Total chunk size observed: **6239 bytes**.

Header size: **31 bytes**.
Pattern slot size: **194 bytes**.

### 6e. Pattern Slot Layout

Each of the 32 slots has a 2-byte header followed by 16 steps:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | Enable flag | Non-zero when the slot is used |
| 0x01 | 1 | Length | Number of steps, usually `16` (`0x10`) |
| 0x02 | … | Step data | Format depends on device |

Slot index → bank/index mapping:

```
bank        = slot / 8   // 0=A, 1=B, 2=C, 3=D
patternIndex = slot % 8   // 0–7
```

#### TB-303 step format

Each step is **2 bytes**: `[note, flags]` × 16 steps.

| Byte | Field |
|------|-------|
| 0 | Note value (maps directly to MIDI; `0` treated as rest) |
| 1 | Flags — bit 0 = accent, bit 1 = slide |

#### TR-808 / TR-909 step format

Each step is **12 bytes**. The observed layout is:

| Byte | Drum |
|------|------|
| 0 | Bass Drum (BD) hit / flag |
| 1 | Bass Drum velocity / accent / tweak (also treated as a BD indicator) |
| 2 | Snare Drum (SD) |
| 3 | Low Tom (LT) |
| 4 | Mid Tom (MT) |
| 5 | Hi Tom (HT) |
| 6 | Closed Hat (CH) |
| 7 | Open Hat (OH) |
| 8 | Clap / Claves (CL) |
| 9 | Clap / Crash (CP / CC) |
| 10 | Maracas / Ride (MA / RC) |
| 11 | Rimshot / Reverse cymbal (RS) |

For the WASM engine the eight primary voices are packed into a single drum-hit
bitfield stored in `StepData.note`:

| Bit | Drum |
|-----|------|
| 0 | BD |
| 1 | SD |
| 2 | LT |
| 3 | MT |
| 4 | HT |
| 5 | CH |
| 6 | OH |
| 7 | CL |

A step is active when any of the mapped bytes is non-zero.

---

## 7. Arrangement (`TRKL` Container)

The arrangement lives inside a nested `CAT ` container whose marker is `TRKL`.
It contains one or more `TRAK` chunks. The exact event format of `TRAK` chunks
is still being reverse-engineered; the parser currently recognises the
container but does not yet derive `ArrangementBar` entries from it.

Observed `TRAK` chunks vary in size and appear to encode per-device pattern
changes and automation events. Work is in progress to map their event stream
to the public `ArrangementBar` structure.

---

## 8. Checklist for Parser Implementation

- [x] Validate root `CAT ` / `RB40` container
- [x] Parse HEAD chunk and detect v2.0 / v2.0.1
- [x] Read title, author, info text, creator URL
- [x] Read MIXR mixer state
- [x] Read 4 device state blocks
- [x] Read all 32 pattern slots per device
- [x] Decode TB-303 notes / accent / slide
- [x] Decode 808/909 drum hits
- [ ] Parse `TRAK` chunks into arrangement bars
- [ ] Skip or parse automation events
- [ ] Identify and decode BPM field in GLOB
- [ ] Validate all offsets and lengths (prevent buffer over-read)

---

## 9. References

- Propellerhead Software. *ReBirth RB-338 v2.01 Manual* (PDF)
- [ReBirth Museum](https://www.rebirthmuseum.com/) — abandonware download + history
- [Peff.com ReBirth Resources](https://web.archive.org/web/*/http://peff.com) (Wayback Machine)
- [Happy TB303 Patterns](https://www.synthtopia.com/content/2008/12/23/free-patterns-and-midi-files-for-tb-303-tr-909-and-tr-808/)
