# Golden references

Short reference songs and their renders, for the comparison tests in
`src/wasm/cpp/tests/test_golden.cpp`. Each case is a `<id>.rbs` song and a
`<id>.wav` reference (44.1 kHz, mono, 16-bit). The test renders the `.rbs`
with this engine and checks it against the `.wav` by two distances
(`cpp/tests/GoldenCompare.h`):

- **Spectral** — 32 log-spaced bands, 40 Hz – 16 kHz, from 2048-point Hann
  frames at hop 512; the mean absolute dB difference over every band and
  frame either signal fills (above −80 dB).
- **Envelope** — the Pearson correlation of the two RMS envelopes in dB
  (256-sample windows), which follows rhythm, accents, gates and decays.

Bit-exact comparison is not the goal: a recording of original ReBirth will
never match sample for sample. These two numbers say whether a render has
the same spectrum and the same shape in time.

| Case                   | Length | Exercises                                           |
| ---------------------- | ------ | --------------------------------------------------- |
| `303-accent-run`       | 1.0 s  | TB-303 accents in runs of four, high resonance      |
| `303-slide-run`        | 1.0 s  | slides, a tie, square wave                          |
| `909-flam`             | 1.0 s  | 909 snare and rim flams against plain hits          |
| `909-shuffle-000`      | 0.5 s  | closed hats on every sixteenth, shuffle 0           |
| `909-shuffle-064`      | 0.5 s  | the same at shuffle 64                              |
| `909-shuffle-127`      | 0.5 s  | the same at shuffle 127                             |
| `303-distortion-sweep` | 1.0 s  | a held 303 note through DIST, drive automated 0–127 |

`standard-rebirth-303b.f32raw` is the older TB-303 regression render
(`test_tb303_golden.cpp`), compared by RMS and spectral centroid.

## Where the references come from

Today every `.wav` is an **engine render** (`Source::Engine` in
`cpp/tools/GoldenCases.h`): a regression net with tight tolerances (1 dB,
r ≥ 0.99), so a change to the sound fails the test until its references are
regenerated on purpose. The test also checks that the harness can tell the
shuffle-0 and shuffle-127 renders apart.

The real target is **original ReBirth 2.0.1**. Help wanted: anyone with a
working Windows 98/XP VM or a period Mac can replace an engine render with
a recording.

## Contributing a ReBirth 2.0.1 recording

1. Load `<id>.rbs` in ReBirth 2.0.1 (the songs are plain v2 files: song or
   pattern mode as saved, every FX off unless the case uses it, 125 BPM).
2. Record the master output from bar 1 at 44.1 kHz for the case's length,
   trimmed so the first sample is the downbeat. Mono (left + right averaged)
   16-bit WAV.
3. Replace `<id>.wav`, and in `cpp/tools/GoldenCases.h` set the case's
   `source` to `Source::Rebirth201` with tolerances that the current engine
   meets — say what it measured in the pull request (the test prints both
   numbers). Those tolerances then shrink as the engine is calibrated.
4. Say which ReBirth build, OS and audio path made the recording.

## Regenerating

After an intended change to a case's song or to the sound:

```bash
cmake --build src/wasm/cpp/build --target rbs-golden
./src/wasm/cpp/build/rbs-golden songs  src/wasm/test-fixtures/golden   # the .rbs files
./src/wasm/cpp/build/rbs-golden render src/wasm/test-fixtures/golden   # engine .wav files
```

`render` never overwrites a ReBirth recording. Listen to what changed
(`rbs-render` renders any song) before committing new references with the
change that caused them. CI checks that the committed `.rbs` files are
byte-for-byte what `rbs-golden songs` builds.
