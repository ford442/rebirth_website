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

| Offset | Size | Field         | Description                                                 |
| ------ | ---- | ------------- | ----------------------------------------------------------- |
| 0x00   | 4    | Chunk ID      | `"CAT "` (0x43 0x41 0x54 0x20)                              |
| 0x04   | 4    | Chunk Size    | Big-endian uint32 — size of the container body that follows |
| 0x08   | 4    | Format Marker | `"RB40"` (0x52 0x42 0x34 0x30)                              |

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

| ID     | Purpose                                                  |
| ------ | -------------------------------------------------------- |
| `HEAD` | File/version header                                      |
| `GLOB` | Global song settings + title                             |
| `USRI` | Author / info text                                       |
| `CAT ` | Nested container (device list `DEVL`, track list `TRKL`) |

Unknown chunks should be skipped.

---

## 3. HEAD Chunk

| Offset | Size | Field             | Description                             |
| ------ | ---- | ----------------- | --------------------------------------- |
| 0x00   | 4    | Signature         | `"[T[T"` (0x5b 0x54 0x5b 0x54)          |
| 0x04   | …    | Header data       | Version-specific fields                 |
| 0x06   | 1    | Sub-format marker | `0x01` or `0x02` in observed v2.x files |

The byte at offset `0x06` inside the HEAD chunk data is the main version
marker seen in the wild:

| Value  | Mapped version |
| ------ | -------------- |
| `0x01` | v2.0           |
| `0x02` | v2.0.1         |

The rest of the 256-byte HEAD chunk is currently reserved / unused by the
parser.

---

## 4. GLOB Chunk — Global Settings & Title

| Offset | Size | Field        | Description                                                   |
| ------ | ---- | ------------ | ------------------------------------------------------------- |
| 0x00   | 1    | Play mode    | `0` = pattern, `1` = song                                     |
| 0x01   | 1    | Loop enabled | `0` = off, `1` = on                                           |
| 0x02   | 4    | Tempo        | Big-endian uint32, BPM multiplied by 1000                     |
| 0x06   | 4    | Loop start   | Big-endian uint32, bar position multiplied by 768             |
| 0x0a   | 4    | Loop end     | Big-endian uint32, bar position multiplied by 768             |
| 0x0e   | 1    | Shuffle      | `0x00`–`0x7f`                                                 |
| 0x0f   | 65   | Mod name     | Null-terminated string (`Standard ReBirth` for the stock mod) |
| 0x50   | 201  | Mod FTP URL  | Null-terminated string                                        |
| 0x119  | 201  | Mod web URL  | Null-terminated string                                        |
| 0x1e2  | 1    | Vintage mode | `0` = ReBirth 2.0 sound, `1` = vintage sound                  |
| 0x1e3  | 29   | Reserved     | Documented as zero; uninitialised memory in many files        |

The layout is Propellerhead's RBS 4.2 document as transcribed by the jsynth
reference parser, and the archive agrees with it. Across a random sample of
193 v2 songs from the archive index (the "corpus" below):

- **Play mode** is `1` (song) in 187 and `0` (pattern) in 6.
- **Byte 0x01 is the loop switch**, `1` in 135 and `0` in 58. The parser used
  to read it as "show info on open", which lives in USRI (§5). The locators
  are bar-aligned in all 193; in most loop-on songs the loop runs from bar 0
  to at or near the song's end, which is why a song "plays forever" in
  ReBirth.
- **Shuffle** spreads across the range; 56 and 64 are the most common.
- **Vintage** is `0` in 177, `1` in 15 and `2` in one file; the parser reads
  any non-zero byte as on. Bytes past it are uninitialised heap memory, so
  nothing past `0x1e2` is read.

The engine honours the play mode, the loop and the shuffle (`Sequencer.h`): song
mode plays the arrangement and stops after its last bar, or jumps from the
loop end back to the loop start when the loop is on and starts inside the
arrangement (a loop parked past the end, like `blue-planet`'s 529–530 in a
530-bar song, is still taken — it starts inside). Pattern mode plays each
device's selected pattern and ignores the arrangement and its automation.
Shuffle delays every even sixteenth by `shuffle / 127 × ⅓` of a step on
patterns whose shuffle flag (§6e) is set — at 127 the off-beat sixteenth sits
on the triplet grid. The ⅓ is an estimate awaiting a golden render.

The vintage flag round-trips but does not change the sound: what ReBirth's
"vintage" (1.x) voice tables did differently is not documented, and none of
the archive's songs pin it down.

The current public `ParsedSong.title` retains the archive's established use of
the mod-name field at `0x0f`. Tempo is rejected when it falls outside ReBirth's
documented 20–500 BPM range.

---

## 5. USRI Chunk — Author & Info Text

The USRI chunk is **712 bytes** in every archive file, with fixed fields:

| Offset | Size | Field             | Description                                       |
| ------ | ---- | ----------------- | ------------------------------------------------- |
| 0x000  | 41   | Window title      | Null-terminated; read as `author`                 |
| 0x029  | 401  | Song information  | Null-terminated; read as `infoText` (`0x0d` = CR) |
| 0x1ba  | 101  | Web page          | Null-terminated URL (not decoded)                 |
| 0x21f  | 1    | Show info on open | `0` = no, `1` = yes                               |
| 0x220  | 168  | Reserved          | Uninitialised memory in many files                |

The show-info byte is `0`/`1` in all 193 corpus files (1 in 170). The strings
are Latin-1 / Windows-1252 and are converted to UTF-8 for display. Each is
read only within its field and written cut to it (40 / 400 characters), so a
round trip cannot shift the flag. A USRI shorter than the fixed layout (older
saves from this project's writer) is read the old way: author, padding zeros,
info text.

---

## 6. Device State & Patterns (`DEVL` Container)

Device data lives inside a nested `CAT ` container whose marker is `DEVL`.
Inside `DEVL` the chunks are:

| ID     | Purpose                                                 |
| ------ | ------------------------------------------------------- |
| `MIXR` | Mixer levels, mutes, pan, FX sends for all four devices |
| `DELY` | Delay settings                                          |
| `PCF ` | PCF settings                                            |
| `DIST` | Distortion settings                                     |
| `COMP` | Compressor settings                                     |
| `303 ` | TB-303 state + 32 patterns (×2)                         |
| `808 ` | TR-808 state + 32 patterns                              |
| `909 ` | TR-909 state + 32 patterns                              |

### 6a. MIXR Chunk

| Offset    | Size | Field                 | Description                    |
| --------- | ---- | --------------------- | ------------------------------ |
| 0x00      | 1    | Master level          | 0–127                          |
| 0x01      | 1    | Compressor device     | 0 none, 1 master, 2–5 303A…909 |
| 0x02      | 1    | PCF device            | 0 off, 1 master, 2–5 303A…909  |
| 0x03–0x0f | 13   | Reserved              |                                |
| 0x10      | 12   | Device 0 mixer record | 303-A                          |
| 0x1c      | 12   | Device 1 mixer record | 303-B                          |
| 0x28      | 12   | Device 2 mixer record | 808                            |
| 0x34      | 12   | Device 3 mixer record | 909                            |

Each 12-byte mixer record:

| Offset | Field                   |
| ------ | ----------------------- |
| 0      | Mute (0 = muted)        |
| 1      | Level 0–127             |
| 2      | Pan 0–127 (64 = centre) |
| 3      | Delay send 0–127        |
| 4      | Distortion send (0/1)   |
| 5–11   | Reserved                |

The compressor and the PCF are **single inserts**, not per-channel sends:
bytes 1 and 2 name the one device each sits on, or the master bus. Bytes 5
and 6 of the records were read as PCF / compressor sends; they are zero in
770 of the corpus's 772 records, so the PCF and the compressor never engaged
in any song. The corpus places the compressor on the master in 58 songs and
on a device in 47; the PCF on a device in 37. `ParsedSong.fx.comp.target` /
`fx.pcf.target` hold the ids and `Mixer` applies the insert there: on a
device after its level (PCF before), or on the stereo master before the
master level and limiter.

### 6a-ii. Global FX chunks (inside `DEVL`)

Observed payload bytes from `standard-rebirth.rbs`:

| Chunk  | Size | Field layout (bytes 0…)                                                  |
| ------ | ---- | ------------------------------------------------------------------------ |
| `DELY` | 8    | enabled, steps (1–32), unit (0 = triplet, 1 = straight), feedback, pan   |
| `PCF ` | 12   | enabled, frequency, Q, amount, pattern (0–54), decay, type (0 BP / 1 LP) |
| `DIST` | 8    | enabled, amount, shape (RBS 4.2: "0x00 = 1.5 mode")                      |
| `COMP` | 8    | enabled, ratio, threshold, attack (observed only `0x00` / `0x7f`)        |

Example payloads (hex):

| Chunk  | Payload                    |
| ------ | -------------------------- |
| `DELY` | `0103013e50000000`         |
| `PCF ` | `014b4f401843010000000000` |
| `DIST` | `01201d0000000000`         |
| `COMP` | `01207f7f00000000`         |

All scalar fields are 0–127. The parser stores them on `ParsedSong.fx`
(`SongFxSettings`) and the mixer reads them through `Mixer::setSongFx()`.

- **DELY** byte 1 was read as a subdivision index and byte 4 as a wet level.
  They are the delay time in steps and the delay return's pan: a step is a
  sixteenth, or two thirds of one with the triplet unit. The corpus's
  delays are 1–12 steps, 3 (a dotted eighth) in 162 of 193. The mixer
  returns the delay at unity, constant-power panned; its line holds 8 s, and
  a longer tap (32 steps below 60 BPM) is clamped. ReBirth's delay has a pan,
  not a ping-pong mode, so none is offered.
- **PCF** stands for _pattern-controlled filter_: byte 4 picks one of 55
  trigger patterns that fire its envelope, byte 5 is that envelope's decay
  and byte 6 the filter type. The 55 patterns are not in the file and are
  not documented, so the engine's PCF still derives its envelope from the
  signal (a follower whose release follows DECAY); the type and decay are
  honoured and the pattern round-trips without changing the sound yet.
- **DIST** byte 2 is called _shape_ in RBS 4.2. The engine still uses it as
  the parallel distortion bus's wet level, as it always has; what ReBirth's
  shape does to the curve is not established. The waveshaper runs 4×
  oversampled with first-order ADAA (`dsp/OversampledShaper.h`), cutting the
  aliasing above 10 kHz by 34–60 dB on 3–7 kHz tones; channels that feed it
  have their dry signal delayed by its latency so the parallel mix does not
  comb-filter.
- **COMP** byte 3 is read as attack (0.5–50 ms in the engine); with only two
  values in the corpus it may be a fast/slow switch.

### 6b. TB-303 Device Chunk (`303 `)

Total chunk size observed: **1097 bytes**.

| Offset | Size          | Field            | Description                       |
| ------ | ------------- | ---------------- | --------------------------------- |
| 0x00   | 1             | Enabled          | `0` = muted                       |
| 0x01   | 1             | Selected pattern | Flat slot 0–31 (`bank × 8 + idx`) |
| 0x02   | 1             | Tune             | 0–127                             |
| 0x03   | 1             | Cutoff           | 0–127                             |
| 0x04   | 1             | Resonance        | 0–127                             |
| 0x05   | 1             | EnvMod           | 0–127                             |
| 0x06   | 1             | Decay            | 0–127                             |
| 0x07   | 1             | Accent           | 0–127                             |
| 0x08   | 1             | Waveform         | 0 = saw, 1 = square               |
| 0x09   | 32 × 34 bytes | Pattern slots    | See below                         |

The drum machines use the same first two bytes (enabled, selected pattern),
then byte 2 is the machine's ACCENT knob and the rest its kit (§6c, §6d).

Header size: **9 bytes**.
Pattern slot size: **34 bytes**.

### 6c. TR-808 Device Chunk (`808 `)

Total chunk size observed: **6238 bytes**.

Header size: **30 bytes**.
Pattern slot size: **194 bytes**.

Each header byte is also the TRAK controller id of that knob on the 808's
track (§7), so the header is the knob state at save time and the track its
automation. `cpp/parser/DrumKit.h` names them (`Kit808::…`):

| Byte | Knob             | Byte | Knob           | Byte | Knob           |
| ---- | ---------------- | ---- | -------------- | ---- | -------------- |
| 0x00 | Enabled          | 0x0a | LT tuning      | 0x14 | CP level       |
| 0x01 | Selected pattern | 0x0b | LT/LC selector | 0x15 | CP/MA selector |
| 0x02 | Accent level     | 0x0c | MT level       | 0x16 | Cowbell level  |
| 0x03 | BD level         | 0x0d | MT tuning      | 0x17 | Cymbal level   |
| 0x04 | BD tone          | 0x0e | MT/MC selector | 0x18 | Cymbal tone    |
| 0x05 | BD decay         | 0x0f | HT level       | 0x19 | Cymbal decay   |
| 0x06 | SD level         | 0x10 | HT tuning      | 0x1a | OH level       |
| 0x07 | SD tone          | 0x11 | HT/HC selector | 0x1b | OH decay       |
| 0x08 | SD snappy        | 0x12 | RS level       | 0x1c | CH level       |
| 0x09 | LT level         | 0x13 | RS/CL selector | 0x1d | ? (0, 70, 127) |

The five selectors are 0/1 in every corpus file. The majority setting is
taken as the unswitched one: 0 = tom (111 / 108 / 116 of 193) and 1 = rim
shot / hand clap (116 / 114), so a set tom selector plays the conga and a
cleared RS/CL or CP/MA selector the claves or the maracas.

### 6d. TR-909 Device Chunk (`909 `)

Total chunk size observed: **6239 bytes**.

Header size: **31 bytes**.
Pattern slot size: **194 bytes**.

| Byte | Knob             | Byte | Knob     | Byte | Knob           |
| ---- | ---------------- | ---- | -------- | ---- | -------------- |
| 0x00 | Enabled          | 0x0b | LT level | 0x16 | Hi-hat level   |
| 0x01 | Selected pattern | 0x0c | LT tune  | 0x17 | CH decay       |
| 0x02 | Accent level     | 0x0d | LT decay | 0x18 | OH decay       |
| 0x03 | BD level         | 0x0e | MT level | 0x19 | Crash level    |
| 0x04 | BD tune          | 0x0f | MT tune  | 0x1a | Crash tune     |
| 0x05 | BD attack        | 0x10 | MT decay | 0x1b | Ride level     |
| 0x06 | BD decay         | 0x11 | HT level | 0x1c | Ride tune      |
| 0x07 | SD level         | 0x12 | HT tune  | 0x1d | Flam interval  |
| 0x08 | SD tune          | 0x13 | HT decay | 0x1e | ? (mostly 100) |
| 0x09 | SD tone          | 0x14 | RS level |      |                |
| 0x0a | SD snappy        | 0x15 | CP level |      |                |

`DeviceState::kit` holds bytes 3.. raw (27 on the 808, 28 on the 909) so a
save writes them back unchanged, unknown bytes included. The drum voices map
a level byte to gain with 100 as unity, a tune byte to ±½ octave around 64,
and a decay byte onto each instrument's decay range; the machine-wide TUNE
and DECAY knobs in the studio are session macros on top (ReBirth has none).

### 6e. Pattern Slot Layout

Each of the 32 slots has a 2-byte header followed by 16 steps:

| Offset | Size | Field     | Description                            |
| ------ | ---- | --------- | -------------------------------------- |
| 0x00   | 1    | Shuffle   | Non-zero: the pattern plays shuffled   |
| 0x01   | 1    | Length    | Number of steps, usually `16` (`0x10`) |
| 0x02   | …    | Step data | Format depends on device               |

Slot index → bank/index mapping:

```
bank        = slot / 8   // 0=A, 1=B, 2=C, 3=D
patternIndex = slot % 8   // 0–7
```

#### TB-303 step format

Each step is **2 bytes**: `[note, flags]` × 16 steps.

| Byte | Field                                                   |
| ---- | ------------------------------------------------------- |
| 0    | Note value (maps directly to MIDI; `0` treated as rest) |
| 1    | Flags — bit 0 = accent, bit 1 = slide                   |

#### TR-808 / TR-909 step format

Each step is **12 bytes**, one per instrument with the accent first — see
"Drum step rows" below for the layout, the decode and the evidence. For the
WASM engine the instruments are packed into `StepData.note` / `drumExtra`:

| Bit (`note`) | Drum                         | Bit (`drumExtra`) | Drum                       |
| ------------ | ---------------------------- | ----------------- | -------------------------- |
| 0            | BD                           | 0                 | CP (808: or maracas)       |
| 1            | SD                           | 1                 | CY (808 cymbal / 909 ride) |
| 2            | LT                           | 2                 | RS (808: or claves)        |
| 3            | MT                           |                   |                            |
| 4            | HT                           |                   |                            |
| 5            | CH                           |                   |                            |
| 6            | OH                           |                   |                            |
| 7            | CB (808 cowbell / 909 crash) |                   |                            |

A step is active when it hits any instrument.

The slot's byte 0 shuffle flag is set on 1,195 of the corpus's 6,176 909
slots and 1,159 of its 6,176 808 slots, and on the 303's too
(`standard-rebirth`'s A1–A3).

#### Drum step rows — accent first

Each step is 12 bytes in the order of the original machines' instrument
select dial, **accent first**:

```
808:  AC BD SD LT MT HT RS CP CB CY OH CH
909:  AC BD SD LT MT HT RS CP CH OH CR RD
```

The parser maps every row onto the instrument bitfield above (BD SD LT MT HT
CH OH CB in `note`, CP CY RS in `drumExtra`; CB is the 808 cowbell and the
909 crash, CY the 808 cymbal and the 909 ride — `synth/DrumBitfield.h`). The
AC byte becomes `StepData.accent`: it accents every hit on the step, and a
step with only an AC byte is kept as an inactive accent. `cpp/parser/RbsDrumRow.h`
holds the table the parser and writer share.

The 808 stores 0/1. The 909 stores **1 = hit, 2 = accented hit, 3 = flam**;
its AC byte is only ever 0 or 2. Accented and flammed 909 hits land in
`StepData.drumAccent` / `drumFlam`, one bit per instrument in bitfield order.

This used to be an open question: the rows were read as `BD BD SD LT MT HT CH
OH CL CP MA RS`, which made every kick an accent and nothing else one. The
corpus (193 songs, 12 bytes × 16 steps × 32 slots each) settles it:

- Under accent first the most frequent instrument on both machines is the
  closed hat (808 byte 11: 20,558 hits; 909 byte 8: 11,387), as it is in
  dance music. Under the old reading byte 11 was the 808's _rim shot_.
- The 909's byte 0 takes only `0` or `2` (13,562 times `2`): the value an
  AC row stores, never a plain hit.
- Value 3 is rare and concentrated where flams are played — snare (534),
  rim shot (237) — against 164 on the kick.
- Value 2 (accented) dominates the 909 kick (8,673 vs 3,980 plain) and is
  the minority on the hats (4,059 vs 7,254), as accented kicks over plain
  hats would be.
- `blue-planet`'s counterexample — 64 hits in byte 0, 43 steps with nothing
  else — is an 808 whose mixer level is 0: an unused machine holding an AC
  lane, not a kick line.

Which of 1 and 2 is the accented value, and whether 3 is a flam at the plain
or the accented level, is inferred from these statistics rather than from a
file saved with known patterns. A reference song saved by ReBirth 2.0.1 with
one known hit per instrument and value would confirm it.

The engine plays the AC row as the ACCENT knob's boost on every hit of the
step (808 and 909), a 909 instrument accent as a further fixed boost, and a
909 flam as a quieter grace hit on the step followed by the full hit FLAM
INTERVAL later (2–40 ms across the knob — an estimate). The closed hat
chokes the open hat on both machines.

## 7. Arrangement (`TRKL` Container)

The arrangement lives inside a nested `CAT ` container whose marker is `TRKL`.
It contains nine `TRAK` chunks in fixed order: mixer, TB-303 A, TB-303 B,
TR-808, TR-909, delay, distortion, PCF, and compressor.

Each TRAK body is:

| Field            | Encoding                 | Description                      |
| ---------------- | ------------------------ | -------------------------------- |
| Event count      | 4-byte big-endian uint32 | Number of following events       |
| Delta position   | 1–5 byte big-endian VLQ  | Delta from the previous event    |
| Controller ID    | 1 byte                   | Track-local parameter identifier |
| Controller value | 1 byte                   | Track-local value                |

Only the delta position is variable-length. Its stored unit represents 24 of
the documented 192-PPQN ticks, so one 4/4 bar is `768 / 24 = 32` encoded units.
All observed pattern-selection events occur on bar boundaries.

Controller ids are the byte offset of the field in the track's DEVL chunk:
the device header on tracks 1–4 (`0x00` enabled, `0x01` selected pattern,
then the knobs of §6b–§6d — on a drum track `0x02` is ACCENT and `0x03..`
the kit), the FX chunk on tracks 5–8, and on the mixer track 0 MIXR's
`0x00` master level, `0x01` compressor device and `0x02` PCF device, then six
controllers per channel from `0x06` (level, pan, delay send, distortion,
two not decoded). The scheduler applies all of these. Controller `0x00` on
a device track is the arrangement's mute lane: in the fixtures devices
drop in and out with it (`standard-rebirth`'s 303-B is off until bar 24),
and it used to be ignored, so every device played from bar 1. Drum-track
controllers used to be applied as TB-303 knobs (`0x02` as TUNE, `0x06` as
DECAY), scrambling 808/909 automation.

For instrument tracks 1–4, controller `0x01` selects a flat pattern slot
`0`–`31`. The parser samples that state at each bar boundary and converts it to
`PatternRef { bank: slot / 8, index: slot % 8 }`. Other controllers are TRAK
automation: they are decoded into `ParsedSong.automation` (C++-only; not copied
through Embind). `RbsAudioEngine::loadSong` / `loadSongFromBytes` publish that
vector on the engine snapshot, and `AutomationScheduler` applies the events on
the audio thread (cutoff, resonance, mixer level/pan, FX). JavaScript archive
loads must call `loadSongFromBytes` so this vector is never dropped by an
Embind round-trip.

A 5-byte chunk id `STRAK` (`STRA` + `K`) is an alias of `TRAK` with the same
body layout. The parser consumes the extra `K` before the size field and
parses the body with `parseTrak`. IFF padding is computed from the 9-byte
header plus body, not from the 4-byte-id rule. Archive fixtures such as
`no-remorse.rbs` may contain the substring `STRAK` at a `TRAK` boundary
without being a 5-byte chunk.

The layout and controller IDs are corroborated by Propellerhead's freely
distributed RBS 4.2 format document and the independent
[jsynth reference parser](https://github.com/nsauzede/jsynth).

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
- [x] Parse `TRAK` chunks into arrangement bars
- [x] Parse TRAK automation into `ParsedSong.automation` for in-engine playback
- [x] Parse 5-byte `STRAK` as a `TRAK` alias
- [x] Identify and decode BPM field in GLOB
- [x] Validate all parsed offsets and lengths (prevent buffer over-read)
- [x] Parse v1 / v1.5 Propellerhead MIDI-container songs (`MThd` + SysEx payload)
- [x] Decode `DELY` / `PCF` / `DIST` / `COMP` FX chunks into `SongFxSettings`
- [x] Serialise a `ParsedSong` back to a v2.x container (§10, `RbsWriter`)
- [x] Decode GLOB play mode, loop, shuffle, vintage, mod FTP URL; USRI show-info
- [x] Decode MIXR insert targets, DELY steps/unit/pan, PCF pattern/decay/type, COMP attack
- [x] Decode the 808/909 kit headers and accent-first step rows (909 accent / flam)
- [ ] Decode the v1 / v1.5 song track (SMF CC 14 pattern chain, §9)
- [ ] Write the v1 / v1.5 MIDI-SysEx container (read-only today)

---

## 9. v1 / v1.5 MIDI-container songs

Legacy `.rbs` files (ReBirth 1.0–1.5) are Standard MIDI Files with a single
Propellerhead SysEx payload. Detection rules:

1. File begins with `MThd`
2. Meta track contains `File version 3.1.0` or `3.2.0` and
   `Propellerhead Software`
3. Generic SMF files (without that meta) are rejected

The SysEx body is optionally wrapped in a MIDI 7-bit escape (`F0 81 … 30 …`).
After stripping wrappers, song data begins with a fixed **309-byte header**
followed by a DEVL-equivalent block:

| DEVL offset | Content                                         |
| ----------- | ----------------------------------------------- |
| 0x00        | `MIXR` mixer (64 bytes)                         |
| 0x40        | `DELY` (8), `PCF ` (12), `DIST` (8), `COMP` (8) |
| 0x64        | `303 ` block ×2 (4681 bytes each)               |
| +9362       | `TR0\08` TR-808 block (6238 bytes)              |
| optional    | `TR0\09` TR-909 block (6239 bytes) when present |

v1 device blocks use ASCII labels (`303 `, `TR0\08`) before a compact state
header. TB-303 pattern slots are **146 bytes** with length in byte 0; v2 slots
keep length in byte 1. Version is inferred from header byte 9 (`0x01` = v1.0,
`0x02` = v1.5).

Each block is exactly label + header + 32 slots: 303 = 4 + 5 + 32 × 146,
808 = 5 + 25 + 32 × 194, 909 = 5 + 26 + 32 × 194. The 909 header used to be
taken as 31 bytes (the v2 size), 5 more than its block holds, so that path
refused every input; it is 26 now, pinned by `static_assert`s. The SysEx
payload is a fixed 18,735 bytes in every fixture — room for both 303s and
the 808, and no 909.

#### The v1 song track (not decoded yet)

All five fixtures parse with an empty arrangement, so they loop one pattern.
The arrangement is not in the SysEx at all: the file is SMF type 1 at **32
ticks per quarter** with one MIDI track per device (channels 0–2: 303 A,
303 B, 808; channel 3 global), and its controller events are the song:
CC 7 / CC 10 the level and pan, CC 12–42 the device knobs, and **CC 14 the
selected pattern** (values 0–7 / 0–24, changing on quarter-note boundaries).
Every track ends at tick 16000. The changes do not fall on bar lines
(`just-15`'s 303 A switches at ticks 1024, 1312, 1504, …), which suggests a
v1 song chains patterns back to back at their own lengths rather than one
per bar. Decoding it needs the v1 303 slot layout first: the 146-byte slot
does not decode cleanly as 16 two-byte steps (several `just-15` slots come
out with length 0 or 1), so the per-pattern lengths that would place each
change cannot be read yet. Both are follow-ups.

---

## 10. Writing `.rbs` (`RbsWriter`)

`cpp/parser/RbsWriter.cpp` is the inverse of this document: `ParsedSong` in,
`.rbs` bytes out. It always emits the v2.x `CAT `/`RB40` container described
above, in the documented chunk order —

```
CAT /RB40
  HEAD  GLOB  USRI
  CAT /DEVL   MIXR DELY PCF  DIST COMP 303  303  808  909
  CAT /TRKL   TRAK ×9
```

— and is reached three ways: `RbsAudioEngine::saveRbs()` (Embind `saveRbs`,
the SAVE .RBS button), the `rbs-write` CLI, and `cpp/tests/test_writer.cpp`.

### The contract is semantic, not bitwise

The gate is **parse → write → parse**, compared field for field
(`RbsCompare.h`): BPM, titles, play mode, loop locators, shuffle, the
vintage flag, the mod FTP URL, show-info, per-device knobs and kits, every
`StepData` (909 dynamics included), pattern shuffle flags, arrangement
`PatternRef`s, FX bytes and insert targets, and automation
`(track, tick, controller, value)`. It holds for 352 of 352 parseable songs
in the corpus sample. The bytes themselves differ from the original file,
and deliberately so:

- Fields this document marks reserved or `?` are written as zero: MIXR's
  bytes 3–15 and each record's bytes 5–11, the GLOB / USRI tails (which
  hold uninitialised memory in the originals) and the USRI web-page field.
- The mod web URL at GLOB `0x119` is written from `creatorUrl`, which is
  read from it.
- Chunks the parser skips on read are **dropped** on write. So is any chunk a
  future ReBirth wrote that this parser does not recognise.
- Chunk padding is recomputed, so chunk offsets shift.
- `STRAK` (the 5-byte `TRAK` alias) is read but always written back as a
  4-byte `TRAK`; the body layout is identical.
- A song with no drum chunk (a v1 song's missing 909, a hand-built song)
  saves the machine's default kit.

### v1 / v1.5 songs are saved as v2.x

The v1/v1.5 MIDI-SysEx container (§9) is read-only. A v1 song saved through
`RbsWriter` comes back as a ReBirth 2.x file with all four device chunks —
the devices the source did not have get empty patterns. The player labels the
download `…-exported.rbs` and says "saved as ReBirth 2.x". Embedding `.rbm`
mods is out of scope: a mod stays a separate file, and a saved song never
grows a sample bank.

### Arrangement → TRAK

`arrangement` is projected back onto controller `0x01` events on tracks 1–4,
one event per bar where that device's pattern changes, at
`bar × TRAK_TICKS_PER_BAR`. Each device track also gets a terminal event on
the final bar boundary: the parser derives the bar count from the highest
TRAK position, so without it an arrangement whose last bars repeat a single
pattern would come back short. That event sits past the last sampled bar, so
it selects nothing.

Automation events are written verbatim onto their own track and merged with
the pattern events in tick order (stable, so parse order is preserved).

### What the writer refuses

It rejects what the parser would refuse to read back, rather than emitting an
unloadable file — an arrangement `PatternRef` outside the 32 slots, an
automation `trackIndex` beyond the nine TRAK tracks, or an automation event
using controller `0x01` on a device track (which would come back as a pattern
change, not automation). `lastError()` says which.

---

## 11. References

- Propellerhead Software. _ReBirth RB-338 v2.01 Manual_ (PDF)
- [ReBirth Museum](https://www.rebirthmuseum.com/) — abandonware download + history
- [Peff.com ReBirth Resources](https://web.archive.org/web/*/http://peff.com) (Wayback Machine)
- [Happy TB303 Patterns](https://www.synthtopia.com/content/2008/12/23/free-patterns-and-midi-files-for-tb-303-tr-909-and-tr-808/)
