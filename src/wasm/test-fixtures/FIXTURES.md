# Parser Test Fixtures

These `.rbs` files are real ReBirth RB-338 songs from the community archive used by
this website. They are included only for parser regression testing.

| File                   | Source URL                                                                                         | Size     | Notes                                        |
| ---------------------- | -------------------------------------------------------------------------------------------------- | -------- | -------------------------------------------- |
| `standard-rebirth.rbs` | `https://test.1ink.us/rb338/archive/rbs-songs/By_Source/Rebirth_2.0/Complete/%23008.rbs`           | 24,904 B | HEAD marker `0x01`; no `STRAK` chunk         |
| `blue-planet.rbs`      | `https://test.1ink.us/rb338/archive/rbs-songs/By_Source/Archives/Hotline%20Archive/%23hardv~1.rbs` | 21,864 B | HEAD marker `0x02`; no `STRAK` chunk         |
| `no-remorse.rbs`       | `https://test.1ink.us/rb338/archive/rbs-songs/By_Source/Archives/Hotline%20Archive/%23primate.rbs` | 21,282 B | HEAD marker `0x02`; contains a `STRAK` chunk |

All three are v2.x format files using the Propellerhead `CAT `/`RB40HEAD` chunk
container. They were chosen because they differ in title/author/info content and
in the sub-format flags observed in the `HEAD`/`GLOB` chunks.

## Mod fixtures (`mods/`)

| File            | Source                         | Size    | Notes                                      |
| --------------- | ------------------------------ | ------- | ------------------------------------------ |
| `minimal.rbm`   | synthetic (`CAT `/`PRBM`)      | 1,596 B | One `EMBF` 808 kick stub + `INFO` title    |

Real archive mods (`test.rbm`, `PITCH_TUNE.rbm`, …) were used to reverse the
container (`src/wasm/cpp/parser/RbmFormat.md`) but are not committed.

## Permission / licensing note

ReBirth RB-338 is abandonware (Propellerhead Software / Reason Studios). These
particular song files have been distributed freely by the ReBirth community for
more than two decades. They are used here in a non-commercial archival context.
If you are the author of one of these files and want it removed, please open an
issue.
