# ReBirth RB-338 `.rbs` File Format Specification

> **Status:** Community reverse-engineered — based on Propellerhead manual research,
> hex-dump analysis of existing `.rbs` files, and the [ReBirth Museum](https://www.rebirthmuseum.com/) archive.
> This document is a living draft. Corrections and additions are welcome.

---

## 1. Overview

An `.rbs` file is a proprietary binary format used by **ReBirth RB-338** (Propellerhead Software, 1997–2005) to store complete songs. It encodes:

- Song metadata (title, author, info text)
- Initial state of all four devices (2× TB-303, TR-808, TR-909)
- Up to 32 patterns per device (8 per bank × 4 banks)
- The song arrangement — an ordered list of which pattern each device plays per bar
- Optional real-time automation (knob movements recorded during playback)

File sizes typically range from **2 KB** (empty song) to **50 KB** (complex arrangement with automation).

---

## 2. File Header

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00   | 17   | Magic | ASCII `"ReBirth Song File"` (no null terminator) |
| 0x11   | 1    | Version | `0x15` = v1.5, `0x20` = v2.0, `0x21` = v2.0.1 |
| 0x12   | 4    | MetadataOffset | Absolute offset to metadata block (little-endian uint32) |
| 0x16   | 4    | DeviceOffset | Absolute offset to first device state block |
| 0x1A   | 4    | PatternOffset | Absolute offset to pattern data region |
| 0x1E   | 4    | ArrangementOffset | Absolute offset to song arrangement |
| 0x22   | 4    | AutomationOffset | Absolute offset to automation data (0 if none) |

**Total header size:** 38 bytes (0x26)

### Version Differences

- **v1.5** — Only TR-808 + 1× TB-303. Second 303 and TR-909 blocks are absent.
- **v2.0** — Full 4-device layout. Device state blocks expanded.
- **v2.0.1** — Identical to v2.0 on-disk; bug-fix release only.

---

## 3. Metadata Block

Located at `MetadataOffset`. Begins with a 2-byte little-endian length field describing the total metadata section size.

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 2 | SectionLength (little-endian uint16) |
| 0x02 | 64 | WindowTitle — null-padded UTF-8 |
| 0x42 | 256 | InfoText — null-padded UTF-8 (shown on song open) |
| 0x142 | 128 | CreatorWebPage — null-padded ASCII URL |
| 0x1C2 | 1 | ShowInfoOnOpen — boolean flag (0 or 1) |

**Note:** Propellerhead used Windows-1252 / MacRoman encoding on some early files. A robust parser should attempt UTF-8 first, then fall back to Latin-1.

---

## 4. Device State Blocks

Four blocks follow contiguously (or via offset table in future versions). Each block is **128 bytes**.

### 4a. TB-303 Block (×2 — A and B)

| Offset (relative) | Size | Field | Range |
|-------------------|------|-------|-------|
| 0x00 | 1 | Tune | 0–127 |
| 0x01 | 1 | Cutoff | 0–127 |
| 0x02 | 1 | Resonance | 0–127 |
| 0x03 | 1 | EnvMod | 0–127 |
| 0x04 | 1 | Decay | 0–127 |
| 0x05 | 1 | Accent | 0–127 |
| 0x06 | 1 | Waveform | 0 = saw, 1 = square |
| 0x07 | 1 | InitialPatternBank | 0–3 |
| 0x08 | 1 | InitialPatternIndex | 0–7 |
| 0x09 | 1 | MixerMute | 0/1 |
| 0x0A | 1 | MixerLevel | 0–127 |
| 0x0B | 1 | MixerPan | 0–127 (64 = centre) |
| 0x0C | 1 | DistSend | 0/1 |
| 0x0D | 1 | PCFSend | 0/1 |
| 0x0E | 1 | CompSend | 0/1 |
| 0x0F | 1 | DelaySend | 0–127 |
| 0x10–0x7F | 112 | Reserved / padding | Zeroes |

### 4b. TR-808 Block

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 12 | BD Level/Tune/Decay (4 bytes each: level, tune, decay, snap) |
| 0x0C | 12 | SD Level/Tone/Snappy |
| 0x18 | 8 | LT Level/Tune/Decay |
| 0x20 | 8 | MT Level/Tune/Decay |
| 0x28 | 8 | HT Level/Tune/Decay |
| 0x30 | 8 | CH/OH Level/Decay |
| 0x38 | 8 | CL/CP/MA/RS Level/Tune |
| 0x40 | 1 | InitialPatternBank |
| 0x41 | 1 | InitialPatternIndex |
| 0x42 | 1 | MixerMute |
| 0x43 | 1 | MixerLevel |
| 0x44 | 1 | MixerPan |
| 0x45 | 1 | DistSend |
| 0x46 | 1 | PCFSend |
| 0x47 | 1 | CompSend |
| 0x48 | 1 | DelaySend |
| 0x49–0x7F | 55 | Reserved |

### 4c. TR-909 Block

Same layout as TR-808 with additional fields:

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 16 | BD Level/Tune/Attack/Decay |
| 0x10 | 16 | SD Level/Tune/Tone/Snappy |
| 0x20–0x3F | 32 | Remaining drums (same order as 808) |
| 0x40 | 1 | AC Level (accent) |
| 0x41 | 1 | FlamAmount |
| 0x42 | 1 | InitialPatternBank |
| 0x43 | 1 | InitialPatternIndex |
| 0x44–0x7F | 60 | Mixer + FX + reserved |

---

## 5. Pattern Data Region

Patterns are stored as a contiguous array. Total count = `numDevices × 32` (8 patterns × 4 banks).

Each pattern begins with a 4-byte header:

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 1 | DeviceId (0–3) |
| 0x01 | 1 | Bank (0–3) |
| 0x02 | 1 | PatternIndex (0–7) |
| 0x03 | 1 | Length (1–16) |

Followed by `Length × 3` bytes of step data:

| Size | Field | Description |
|------|-------|-------------|
| 1 | Note / DrumBits | 303: MIDI note (0 = C-1, 12 = C0 … 72 = C5, 255 = rest). 808/909: bitfield of active drums |
| 1 | Flags | Bit 0 = accent, Bit 1 = slide (303 only), Bits 2–7 = reserved |
| 1 | Velocity | 0–127 (not used by original UI but stored) |

**Total per pattern:** 4 + (Length × 3) bytes.

### TB-303 Note Encoding

The TB-303 uses a non-standard note mapping. The original hardware's internal representation maps:

| Internal Value | Note |
|----------------|------|
| 0–11 | C-1 to B-1 |
| 12–23 | C0 to B0 |
| … | … |
| 72–83 | C5 to B5 |
| 255 | Rest (tie / no note) |

A parser should map these to standard MIDI note numbers for the WASM engine.

### TR-808 / TR-909 Drum Bitfield

For drum machines, the "Note" byte is a bitfield rather than a pitch:

| Bit | Drum (808) | Drum (909) |
|-----|------------|------------|
| 0 | Bass Drum (BD) | Bass Drum (BD) |
| 1 | Snare Drum (SD) | Snare Drum (SD) |
| 2 | Low Tom (LT) | Low Tom (LT) |
| 3 | Mid Tom (MT) | Mid Tom (MT) |
| 4 | Hi Tom (HT) | Hi Tom (HT) |
| 5 | Closed Hat (CH) | Closed Hat (CH) |
| 6 | Open Hat (OH) | Open Hat (OH) |
| 7 | Clap/Claves (CL) | Crash (CC) / Ride (RC) |

---

## 6. Song Arrangement

The arrangement is a sequential list of bars. Each bar defines which pattern each device plays.

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 2 | NumBars (little-endian uint16) |
| 0x02 | … | Bar entries follow |

Each bar entry is **8 bytes**:

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 2 | BarNumber (1-based, little-endian uint16) |
| 0x02 | 1 | 303-A Bank |
| 0x03 | 1 | 303-A Pattern |
| 0x04 | 1 | 303-B Bank |
| 0x05 | 1 | 303-B Pattern |
| 0x06 | 1 | 808 Bank |
| 0x07 | 1 | 808 Pattern |
| 0x08 | 1 | 909 Bank |
| 0x09 | 1 | 909 Pattern |

*(Correction: actually 10 bytes per bar entry — 4 devices × 2 bytes each for bank+pattern, plus 2 bytes for bar number = 10 bytes total.)*

**Note:** ReBirth allows empty / "no change" bars. If a device's bank byte is `0xFF`, that device continues playing its previous pattern.

---

## 7. Automation Data (Optional)

If `AutomationOffset != 0`:

| Offset (relative) | Size | Field |
|-------------------|------|-------|
| 0x00 | 2 | NumEvents (little-endian uint16) |
| 0x02 | … | Event entries |

Each event:

| Size | Field | Description |
|------|-------|-------------|
| 4 | Timestamp | Absolute sample count (uint32 LE) |
| 1 | DeviceId | Which device (0–3) |
| 1 | ControlId | Which knob / button |
| 1 | Value | New value (0–127) |

**Total per event:** 7 bytes.

Automation is **not required** for basic playback. A minimal parser can skip this block.

---

## 8. Checklist for Parser Implementation

- [ ] Validate magic bytes
- [ ] Detect version and adjust offsets (v1.5 has fewer devices)
- [ ] Read metadata (title, author, info text)
- [ ] Read 4 device state blocks (or 2 for v1.5)
- [ ] Read all pattern headers + step data
- [ ] Read arrangement (handle `0xFF` "no change" pattern refs)
- [ ] Skip or parse automation block
- [ ] Map TB-303 internal note values to MIDI
- [ ] Decode 808/909 drum bitfields into per-step drum hits
- [ ] Validate all offsets and lengths (prevent buffer over-read)

---

## 9. References

- Propellerhead Software. *ReBirth RB-338 v2.01 Manual* (PDF)
- [ReBirth Museum](https://www.rebirthmuseum.com/) — abandonware download + history
- [Peff.com ReBirth Resources](https://web.archive.org/web/*/http://peff.com) (Wayback Machine)
- [Happy TB303 Patterns](https://www.synthtopia.com/content/2008/12/23/free-patterns-and-midi-files-for-tb-303-tr-909-and-tr-808/)
