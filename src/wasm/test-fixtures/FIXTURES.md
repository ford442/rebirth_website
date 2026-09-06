# Parser Test Fixtures

These `.rbs` files are real ReBirth RB-338 songs from the community archive used by
this website. They are included only for parser regression testing.

## v2.x IFF fixtures (`CAT `/`RB40HEAD`)

| File                   | Source URL                                                                                         | Size     | Notes                                        |
| ---------------------- | -------------------------------------------------------------------------------------------------- | -------- | -------------------------------------------- |
| `standard-rebirth.rbs` | `https://test.1ink.us/rb338/archive/rbs-songs/By_Source/Rebirth_2.0/Complete/%23008.rbs`           | 24,904 B | HEAD marker `0x01`; no `STRAK` chunk         |
| `blue-planet.rbs`      | `https://test.1ink.us/rb338/archive/rbs-songs/By_Source/Archives/Hotline%20Archive/%23hardv~1.rbs` | 21,864 B | HEAD marker `0x02`; no `STRAK` chunk         |
| `no-remorse.rbs`       | `https://test.1ink.us/rb338/archive/rbs-songs/By_Source/Archives/Hotline%20Archive/%23primate.rbs` | 21,282 B | HEAD marker `0x02`; contains a `STRAK` chunk |

All three are v2.x format files using the Propellerhead `CAT `/`RB40HEAD` chunk
container. They were chosen because they differ in title/author/info content and
in the sub-format flags observed in the `HEAD`/`GLOB` chunks.

## v1 / v1.5 MIDI-container fixtures (`MThd`)

ReBirth 1.0 / 1.5 song files are **not** the v2 IFF container. Official
Propellerhead SongPacks from 1997 are Standard MIDI Type 1 files whose first
track is a copyright meta-event:

```
File version 3.1.0 (c)1997 Propellerhead Software, all rights reserved
File version 3.2.0 (c)1997 Propellerhead Software, all rights reserved
```

`3.1.0` is the 1.0-era pack format; `3.2.0` is the 1.5-era pack format. Song
data lives in subsequent tracks as SysEx (`F0 …`), not as a generic piano-roll
SMF.

`Artists/Cavey/*` files in the public archive use this same container
(`File version 3.2.0` + Propellerhead copyright). They are valid v1.5-era
ReBirth songs, not “mislabeled random MIDI.” A file is generic MIDI (reject)
only when it starts with `MThd` and **lacks** that Propellerhead version
string.

Source for the official packs:

https://archive.org/details/rebirth-rb338-songpacks

| File | Pack origin | Version string | Size | Role |
| ---- | ----------- | -------------- | ---- | ---- |
| `v1/just-15.rbs` | RBSongPack 09 / `Just 15.RBS` | 3.1.0 | 19,705 B | v1 golden |
| `v1/retrograde.rbs` | RBSongPack 02 / `Retrograde.RBS` | 3.1.0 | 20,029 B | v1 golden |
| `v15/gurkensalat.rbs` | RBSongPack 37 / `Gurkensalat.RBS` | 3.2.0 | 19,349 B | v1.5 golden |
| `v15/hermetico-absoluto.rbs` | RBSongPack 18 / `Hermetico Absoluto.RBS` | 3.2.0 | 20,082 B | v1.5 golden |
| `v15/cavey-3-acid.rbs` | archive `Artists/Cavey/3_acid.rbs` | 3.2.0 | 27,163 B | community v1.5 golden |
| `reject/generic-smf.mid` | synthetic empty Type-1 SMF | (none) | 26 B | MIDI rejection fixture |

The raw bytes are vendored as `*.b64` next to each fixture (GitHub text-safe).
Materialize the binary files with:

```
python3 scripts/fetch-v1-parser-fixtures.py
```

SHA-256 checksums: `manifest.json`.

Parser contract:

1. `CAT ` + `RB40` → v2.x IFF path.
2. `MThd` + `File version 3.1.0` / `3.2.0` + `Propellerhead Software` → v1 / v1.5 path.
3. `MThd` without that copyright/version meta → reject as Standard MIDI, not ReBirth.

## Mod fixtures (`mods/`)

| File            | Source                         | Size    | Notes                                      |
| --------------- | ------------------------------ | ------- | ------------------------------------------ |
| `minimal.rbm`   | synthetic (`CAT `/`PRBM`)      | 1,596 B | One `EMBF` 808 kick stub + `INFO` title    |

Real archive mods (`test.rbm`, `PITCH_TUNE.rbm`, …) were used to reverse the
container (`src/wasm/cpp/parser/RbmFormat.md`) but are not committed.

## Permission / licensing note

ReBirth RB-338 is abandonware (Propellerhead Software / Reason Studios). These
particular song files have been distributed freely by the ReBirth community for
more than two decades (official SongPacks on the Propellerhead FTP, later
mirrored on the Internet Archive). They are used here in a non-commercial
archival context. If you are the author of one of these files and want it
removed, please open an issue.
