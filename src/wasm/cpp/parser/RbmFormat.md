# ReBirth RB-338 `.rbm` File Format Specification

> **Status:** Community reverse-engineered — based on hex-dump analysis of
> archive `.rbm` files (`test.rbm`, `PITCH_TUNE.rbm`, `revision2.rbm`,
> `Metallicon.rbm`). This is a **Propellerhead chunk container**, the same
> family as `.rbs` (`RbsFormat.md`), not ZIP and not a Mac resource fork.
> Corrections and additions are welcome.

---

## 1. Overview

An `.rbm` (ReBirth Mod) is a resource bundle packed by Propellerhead
**ModPacker**. It replaces drum samples, optional 303 waveforms, UI stills,
and sometimes a bundled demo `.rbs`.

Typical size: **130 KB** (sample-only test mods) to **several MB** (full skins
+ every drum). Large files are usually JPEGs, not audio.

The AudioWorklet must **never** decode skin graphics. Skins are listed by the
parser and extracted later by the mirror pipeline onto `public/archive/`.

---

## 2. Root container

| Offset | Size | Field         | Description                                      |
| ------ | ---- | ------------- | ------------------------------------------------ |
| 0x00   | 4    | Chunk ID      | `"CAT "` (0x43 0x41 0x54 0x20)                   |
| 0x04   | 4    | Chunk Size    | Big-endian uint32 — body size after this field   |
| 0x08   | 4    | Format marker | `"PRBM"` (Propellerhead ReBirth Mod)             |

This is the `.rbs` layout with `PRBM` instead of `RB40`.

Chunks after `"PRBM"` use the same header as `.rbs`:

```
4-byte ID | 4-byte BE size | size bytes | [pad byte if size is odd]
```

Unknown chunks are skipped.

Observed top-level chunks (inside the root `CAT `/`PRBM`):

| ID     | Purpose                                                |
| ------ | ------------------------------------------------------ |
| `HEAD` | 256-byte header + copyright                            |
| `EMBF` | One embedded file (name + payload)                     |
| `INFO` | 1280-byte Pascal-ish text block (title / description)  |

No nested `CAT ` lists have been required to parse the study set; every
`EMBF` sits directly under the root `PRBM` list.

---

## 3. `HEAD` (256 bytes)

First bytes of every examined file:

```
5b 35 b3 5b  bc 01 00 00  00 28 63 29  31 39 39 38
```

followed by the C-string

```
(c)1998 Propellerhead Software, all rights reserved.
```

The remainder of the 256-byte chunk is zero-filled. Treat `HEAD` as a
presence/version marker; do not fail the parse if the copyright string
differs.

---

## 4. `EMBF` — embedded file

```
<C-string filename> <NUL> <raw file bytes…>
```

There is **no** extra length prefix after the name. The payload runs to the
end of the `EMBF` chunk. Filenames are 8.3-ish or slightly longer
(`tr808bd.aif`, `TR808MT.AIF`, `12522.jpg`, `demo.rbs`). Case is not
significant.

### Payload kinds (by extension)

| Extension        | Kind   | Notes                                      |
| ---------------- | ------ | ------------------------------------------ |
| `.aif` / `.aiff` | sample | IFF `FORM` / `AIFF` (big-endian PCM)       |
| `.wav`           | sample | RIFF WAVE (rare in the study set)          |
| `.jpg` / `.jpeg` | skin   | Photoshop-written UI stills                |
| `.png` / `.gif`  | skin   | later community packs                      |
| `.rbs`           | song   | bundled demo song (`CAT ` / `RB40`)        |
| other            | other  | keep name + bytes, do not invent a slot    |

Phase 1 stores payload bytes as-is. AIFF/WAV decode is a later sample-voice
step.

---

## 5. Sample filename → engine slot

ModPacker used a closed set of names. Match **case-insensitively** after
stripping the extension. Prefixes `tr808`, `808`, `tr909`, `909` are
equivalent.

### TR-808

| Name tokens        | Slot (maps to `DrumHit` / `DrumExtra`) |
| ------------------ | -------------------------------------- |
| `bd`               | kick (`DrumHit::BD`)                   |
| `sd`               | snare (`DrumHit::SD`)                  |
| `lt`               | low tom (`DrumHit::LT`)                |
| `mt`               | mid tom (`DrumHit::MT`)                |
| `ht`               | high tom (`DrumHit::HT`)               |
| `ch` / `hh`        | closed hat (`DrumHit::CH`)             |
| `oh`               | open hat (`DrumHit::OH`)               |
| `rs`               | rimshot (`DrumExtra::RS`)              |
| `cp`               | clap (`DrumExtra::CP`)                 |
| `cb` / `cl`        | clave / cl (`DrumHit::CL`)             |
| `cy`               | cymbal (extra; no Phase-1 bit)         |
| `ma`               | maracas (`DrumExtra::MA`)              |

### TR-909

Same tokens with a `909` prefix: `tr909bd`, `tr909ccy` / `cc` (crash),
`tr909rc` / `rc` (ride). Crash/ride have no Phase-1 bit; they are still
recorded as slots so Phase 2 can play them.

### TB-303

| Name tokens        | Slot        |
| ------------------ | ----------- |
| `303saw` / `saw`   | 303 saw     |
| `303sqr` / `square`| 303 square  |

A file named `tr808bd.aif` is an 808 kick. `TR808MT.AIF` is an 808 mid tom.
Ambiguous short names (`bd.aif`) default to **808**.

---

## 6. `INFO` (1280 bytes)

A fixed-size text block. Observed layouts:

- Description starting at offset 0 (`PITCH_TUNE.rbm`)
- Title-only C-string later in the block (`test.rbm` → `"test"`)

Parser behaviour: collect every NUL-terminated printable string of length ≥ 3.
The first string is `title` if no longer description is present; the longest
string ≥ 20 characters is `description`. Remainder is ignored.

---

## 7. Skin policy

JPEG/PNG payloads are **catalogued only**. They are not decoded in WASM and
must not be copied into the AudioWorklet heap. The `#82` mirror pipeline is
the place to extract stills to

```
public/archive/rbm-mods/<slug>/<original-filename>
```

for CSS `background-image`.

---

## 8. What this is not

- Not ZIP / deflate — **do not vendor miniz** for `.rbm`.
- Not a Mac resource fork (`Rsrc`). Flattened Windows/Mac data-fork copies
  in the archive are still `CAT `/`PRBM`.
- Not an `.rbs` song (`RB40`). A bundled song lives *inside* an `EMBF`.
