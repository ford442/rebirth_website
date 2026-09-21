# WebAssembly Audio Module — `src/wasm/`

In-browser playback engine for ReBirth RB-338 `.rbs` song files.

## Status

> **SHIPPING ENGINE, PARTIAL FEATURE SET**
> CI builds and deploys the browser engine with pinned Emscripten 6.0.3. Run `npm run build:ship` locally to reproduce the shipping build.
> The UI component (`RbsPlayer.astro`) degrades gracefully when WASM is missing: metadata sniffing and sketch preview remain available.

### What the engine does

Parses both `.rbs` generations (v2 `CAT `/`RB40` containers and v1/v1.5
Propellerhead MIDI containers), sequences the arrangement, and synthesises all
four devices: TB-303 A/B through a ZDF ladder filter
(`synth/dsp/ZdfLadder.h`) with PolyBLEP oscillators (`synth/dsp/PolyBlep.h`),
and TR-808 / TR-909 either procedurally (`synth/DrumSynth.*`) or from `.rbm`
mod samples (`RbsAudioEngine::loadMod` → `synth/SamplePool.*`, covered by
`tests/wasm-rbm-mod.spec.ts`). It bounces offline to 16-bit WAV — full mix or
per-device stems — via `renderOfflineToWav` on a dedicated Worker, and the TS
side exports the loaded song as a Standard MIDI File (`src/lib/midi-smf.ts`).

Pattern steps are editable in the studio grid. A click sends one patch —
`RbsAudioEngine::setStep(deviceId, bank, patternIndex, stepIndex, StepData)` —
which mutates the engine's main-thread working copy and republishes the render
graph, the same atomic snapshot swap `loadMod()` uses. Undo lives in JavaScript
(`js/player-step-edit.ts`) as a bounded stack of inverse patches. Nothing sends
a `ParsedSong` back across Embind, and nothing allocates in `processBlock()`.

### What it does not do

- **Write `.rbs`.** The parser is read-only; there is no serialiser, so every
  edit — pattern steps and live knobs alike — is session-only. Reloading the
  file from disk restores the original song.
- **Persist knob moves in the working copy.** `setDeviceParam` replays onto a
  rebuilt graph but does not rewrite `ParsedSong::devices`; only pattern edits
  land in the engine's working copy today. A `SAVE .RBS` control would need to
  snapshot both.
- **Play TRAK automation from an archive file loaded through Embind.** The
  parser _stores_ every non-pattern TRAK event in `ParsedSong::automation` and
  `AutomationScheduler` applies it on the audio thread — but that vector is not
  registered with Embind, so it only survives the `loadSongFromBytes` path
  (parse inside C++). A song handed back through `loadSong(WasmParsedSong)`
  loses it. Archive loads must use `loadSongFromBytes`.
- **Load `.rbm` skins.** Skin resources are counted and reported, never drawn.

## Language roles

New code lands in the layer that owns the job. This table is the rule; when a
change does not fit it, the split is wrong, not the table.

| Language       | Owns                                                                                                      | Must not do                                                             |
| -------------- | --------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| **C++**        | `.rbs` / `.rbm` parsing, DSP, sequencer, mixer, sample decode, WAV byte generation, RT-safe snapshot swap | JPEG skins, DOM, `fetch`, MIDI SMF writing (already TypeScript)         |
| **TypeScript** | `AudioContext`, the Embind boundary, player UI, MiniSearch, the SMF writer, download helpers              | Inner-loop DSP, owning PCM buffers, growing WASM memory                 |
| **Astro**      | Markup, `BASE_URL` links, content collections                                                             | Engine logic — `RbsPlayer.astro`'s `<script>` only calls `initPlayerUI` |

The C++ ↔ TypeScript field contract itself is `CONTRACT.md`, enforced by
`npm run contract:check`.

## Player capability matrix

| Capability                               | WASM engine | Degraded (sketch)     | Degraded (metadata only) |
| ---------------------------------------- | ----------- | --------------------- | ------------------------ |
| Load `.rbs` via drag/drop or file picker | ✅          | ✅                    | ✅                       |
| Show title / author from file header     | ✅          | ✅                    | ✅                       |
| Full ReBirth synthesis (303/808/909)     | ✅          | ❌                    | ❌                       |
| Transport play / pause / stop            | ✅          | ✅ (metronome sketch) | ❌ (play disabled)       |
| Step bar + level meter animation         | ✅          | ✅                    | ❌                       |
| Tempo slider                             | ✅          | ✅                    | ✅ (display only)        |
| Volume slider                            | ✅          | ✅                    | ❌                       |
| Load an `.rbm` mod (drum samples)        | ✅          | ❌                    | ❌                       |
| Bounce to WAV / per-device stems         | ✅          | ❌                    | ❌                       |
| Export `.mid` (pure TypeScript)          | ✅          | ✅                    | ❌                       |

### Init failure reasons

When `WasmAudioBridge.init()` fails, `RbsPlayer` classifies the error and switches to `DegradedRbsPlayer`:

| Reason                | Typical cause                    | User message                |
| --------------------- | -------------------------------- | --------------------------- |
| `unsupported-browser` | No WebAssembly                   | Browser lacks required APIs |
| `wasm-unavailable`    | `public/wasm/` empty (not built) | WASM binaries not built yet |
| `wasm-load-failed`    | 404 / blocked glue script        | WASM assets failed to load  |
| `worklet-unavailable` | No `AudioWorkletNode`            | AudioWorklet not supported  |
| `worklet-init-failed` | Worklet registration error       | Worklet init failed         |
| `engine-init-failed`  | Other engine errors              | Generic degraded fallback   |

Pure-TS metadata parsing lives in `src/wasm/js/RbsMetadataSniffer.ts` (HEAD / GLOB / USRI chunks). Sketch preview uses Web Audio oscillators in `src/wasm/js/DegradedRbsPlayer.ts` — audible metronome clicks, not silent success.

## Integration Roadmap (Phase Plan)

1. **Parser completion** — metadata, patterns, and TRAK arrangement decode from real `.rbs` payloads (`RbsTrak.cpp`) ✅, plus v1 / v1.5 MIDI-container songs ✅. `.rbm` mods are `CAT `/`PRBM` + `EMBF` resource bundles (`RbmParser`, `RbmFormat.md`) ✅, and their samples load into the engine ✅.
2. **Audio engine parity** — procedural TR-808 / TR-909 drums ✅; TB-303 ZDF ladder filter, PolyBLEP oscillators and slide ✅; `.rbm` sample playback through `SamplePool` ✅. Remaining: `.rbm` skin rendering and closer voice-by-voice calibration against hardware.
3. **Realtime control API** — transport + tempo + volume commands flow through a lock-free queue ✅.
4. **Archive demo pipeline** — curated demo `.rbs` files under `public/archive/rbs-songs/demo/` for direct browser previews ✅.
5. **Offline bounce + MIDI export** — WAV mix and per-device stems on a Worker (`js/wasm-bounce.ts`), and `.mid` export from `src/lib/midi-smf.ts` ✅.
6. **Fallback mode** — if WASM init fails, provide metadata sniffing + Web Audio sketch preview so the UI remains usable ✅.
7. **End-to-end validation** — browser tests cover upload, demo loading, transport controls, mod loading and fallback behaviour ✅ (`tests/rbs-player.spec.ts`, `tests/wasm-*.spec.ts`).
8. **Pattern editing** — step edits patch the engine's working copy through `setStep` / `setPatternLength` and republish the graph ✅, with JS-side inverse-patch undo ✅. Remaining: arrangement editing (changing a bar's `PatternRef`) and a `.rbs` writer so `SAVE .RBS` can snapshot pattern edits *and* live knob moves.

## Audio thread architecture

We use **Architecture A — Emscripten Wasm Audio Worklet**.

- **Main thread** (`WasmAudioBridge`):
  - Loads the Emscripten glue and WASM module.
  - Creates the `AudioContext` and wires the `AudioWorkletNode`.
  - Copies `.rbs` bytes onto the WASM heap and calls `RbsAudioEngine::loadSongFromBytes()` so parse and load stay in C++ (TRAK automation never round-trips through Embind). The returned value is a UI summary only.
  - Sends transport/control commands (`play`, `pause`, `stop`, `seek`, `setVolume`, `setTempo`) by pushing them into a lock-free command queue that lives in shared WASM memory.
  - Polls `getPlaybackPosition()` for UI updates.
  - Offline bounce/stems run in a Dedicated Worker that instantiates a **second** copy of the same glue (no AudioWorklet). The live worklet keeps `status=playing` and never holds two `SamplePool` arenas.

- **Audio thread** (`RbsWorklet` / `RbsAudioEngine::processBlock()`):
  - Pins an `EngineSnapshot*` via a lock-free hazard (no `shared_ptr` in the callback).
  - Drains the command queue at the start of every 128-frame render quantum.
  - Generates sample-accurate step events and renders **sub-blocks** at each `sampleOffset`.
  - Mixes to planar stereo (L/R). Master volume uses WASM SIMD128 when compiled with Emscripten.
  - Publishes `bar`/`step` via atomic variables so the main thread can read them without locking.
  - The main thread builds a new snapshot (voices + mixer + song) and retires the old one once the audio epoch has advanced.
  - The hazard handshake (`pinSnapshot` / `reclaimRetiredSnapshots`, `RbsAudioEngine.cpp`) is **sequentially consistent**, not release/acquire. Under release/acquire the audio thread's hazard store may be reordered past its validating load of `m_published`, so a concurrent reclaim can read a stale hazard and free the snapshot the callback is about to read. Pattern editing republishes on every click, which makes that window easy to hit; `tests/test_engine.cpp` stresses it from two threads.

This keeps latency low (128-frame Web Audio quanta), avoids main-thread synthesis work, and matches the existing `-sAUDIO_WORKLET=1` / `-sWASM_WORKERS=1` build configuration.

### Why not Option B or C?

- **Option B** (main-thread WASM + ScriptProcessor/ring buffer) would push synthesis or PCM streaming onto the main thread, creating latency and jank risks for a UI-heavy archive site.
- **Option C** (hybrid: parse on main, synthesise on audio thread) is essentially the same runtime shape as Option A; we capture it explicitly above by stating that `.rbs` parsing stays on the main thread (inside `loadSongFromBytes`) while the audio thread owns synthesis and sequencing.

### Real-time constraints

The audio callback (`processBlock()`) must never:

- allocate heap memory (`malloc` / `new` / `std::vector` resize),
- take a mutex or spin lock,
- call into JS,
- or perform file I/O.

All control data is pre-allocated or passed through the lock-free `EngineCommandQueue`. Position is shared via `std::atomic`. Scratch render buffers are fixed-size stack arrays.

## Quick Start

### Native build (no Emscripten)

Fast offline unit tests and hex-level inspection use the **same C++ sources** as the WASM target (`parser/`, `engine/`, `synth/`). CMake is the primary native build:

```bash
# Configure, build, and run doctest suite (from repo root)
npm run wasm:test

# Or step by step:
cmake -S src/wasm/cpp -B src/wasm/cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build src/wasm/cpp/build --parallel
ctest --test-dir src/wasm/cpp/build --output-on-failure
```

A lightweight Makefile (`src/wasm/cpp/Makefile`) wraps CMake / `build.sh`. All
translation units live in `src/wasm/cpp/sources.cmake` — do not add `.cpp` files
to `CMakeLists.txt`, `Makefile`, or `build.sh` individually. `build.sh` is an
`emcmake` wrapper.

**Inspect a song file** — default output is JSON (`WasmParsedSong`-compatible):

```bash
cmake --build src/wasm/cpp/build --target rbs-inspect
./src/wasm/cpp/build/rbs-inspect src/wasm/test-fixtures/standard-rebirth.rbs
./src/wasm/cpp/build/rbs-inspect --pretty src/wasm/test-fixtures/blue-planet.rbs
```

#### What the native tests cover

| Area                          | Tests                                                  |
| ----------------------------- | ------------------------------------------------------ |
| Header / container validation | Rejects truncated files and missing `CAT`/`RB40` magic |
| Metadata extraction           | Title, author, info text from golden `.rbs` fixtures   |
| Pattern counts                | 32 patterns per device on v2.x fixtures                |
| Sequencer timing              | Bar length in samples at 120/140 BPM (±1 ms)           |
| Engine / mixer / drums        | Transport, voice routing, procedural drum hits         |

Fixtures live in [`test-fixtures/`](test-fixtures/). CI runs this suite in [`.github/workflows/native-cpp.yml`](../../.github/workflows/native-cpp.yml) (no Emscripten, typically under one minute).

### WASM build (Emscripten)

```bash
# 1. Install Emscripten (one-time)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 6.0.3
./emsdk activate 6.0.3
source ./emsdk_env.sh

# 2. Build (release)
npm run wasm:build

# 3. Build (debug)
npm run wasm:build:debug

# 4. Verify outputs exist
ls public/wasm/
# → rbsParser.js  rbsParser.wasm  rbsWorklet.js  wasm-build.json

# 5. Build the complete shipping site (WASM first, then Astro)
npm run build:ship
# → the same four files are copied to dist/wasm/
```

The pinned Emscripten version lives in [`cpp/.emscripten-version`](cpp/.emscripten-version). Update it deliberately when you need a newer toolchain. The build script exits before deleting old artifacts or compiling if `emcc` is missing, its version cannot be detected, or it differs from the pin.

## Build Profiles

The build script (`src/wasm/cpp/build.sh`) supports two modes:

| Mode    | Command                    | Optimisation | Memory growth              | Best for    |
| ------- | -------------------------- | ------------ | -------------------------- | ----------- |
| Release | `npm run wasm:build`       | `-O3 -flto`  | Disabled, fixed 64 MB heap | Shipping    |
| Debug   | `npm run wasm:build:debug` | `-O0 -g3`    | Enabled, 32–128 MB cap     | Development |

Release uses a fixed heap so the audio callback never triggers a memory resize. Debug enables `ASSERTIONS`, `SAFE_HEAP`, `STACK_OVERFLOW_CHECK`, and `WEBAUDIO_DEBUG` to catch memory and worklet issues early.

### Linker flags (CMake SSOT)

All Emscripten compile and link flags live in
[`cpp/CMakeLists.txt`](cpp/CMakeLists.txt). `build.sh` is an `emcmake` wrapper
only.

| Flag / setting                                | Release                            | Debug                  |
| --------------------------------------------- | ---------------------------------- | ---------------------- |
| Optimisation                                  | `-O3 -flto`                        | `-O0 -g3`              |
| `-sASSERTIONS`                                | `0`                                | `1`                    |
| `-sINITIAL_MEMORY`                            | 64 MiB                             | 32 MiB                 |
| `-sMAXIMUM_MEMORY`                            | 64 MiB (equal to INITIAL)          | 128 MiB                |
| `-sALLOW_MEMORY_GROWTH`                       | `0`                                | `1` (max 128 MiB)      |
| `-sMALLOC`                                    | `emmalloc`                         | `emmalloc-memvalidate` |
| `-sFILESYSTEM`                                | `0`                                | `0`                    |
| `-sSTACK_SIZE`                                | 128 KiB (module linear stack)      | 128 KiB                |
| AudioWorklet pthread stack                    | 64 KiB (`AUDIO_THREAD_STACK_SIZE`) | 64 KiB                 |
| `-pthread -sAUDIO_WORKLET=1 -sWASM_WORKERS=1` | yes                                | yes                    |

Heap and dual-build policy: [`docs/adr/0002-wasm-build-variants-and-heap.md`](../../docs/adr/0002-wasm-build-variants-and-heap.md).

### Native tooling

```bash
npm run wasm:native:configure   # writes src/wasm/cpp/build/compile_commands.json
```

**Run this once after cloning.** Root [`.clangd`](../../.clangd) points clangd
at `src/wasm/cpp/build/compile_commands.json`; until that file exists, clangd
guesses include paths and will resolve the wrong `parser/` vs `native_stubs/`
headers, so an editor shows errors the real build does not have. `npm run
wasm:test` regenerates it as a side effect. The database is git-ignored — it
records absolute paths from your machine.

### Emitted files

Emscripten produces `rbsParser.js` and `rbsParser.wasm`. `build.sh` copies the
checked-in [`js/rbs-worklet-bootstrap.js`](js/rbs-worklet-bootstrap.js) to
`rbsWorklet.js` and does **not** patch generated glue. `WasmAudioBridge`
`locateFile` remaps worklet-scope glue requests. `wasm-build.json` records
mode, heap sizes, and artifact byte counts.

### Deployment paths

Assets live in `public/wasm/` and are served from the site's base path. With `base: '/rebirth_website'` (the canonical GitHub Pages deployment), the runtime URLs become:

| File           | Public                        | Served at                               |
| -------------- | ----------------------------- | --------------------------------------- |
| Glue           | `public/wasm/rbsParser.js`    | `/rebirth_website/wasm/rbsParser.js`    |
| WASM           | `public/wasm/rbsParser.wasm`  | `/rebirth_website/wasm/rbsParser.wasm`  |
| Worklet        | `public/wasm/rbsWorklet.js`   | `/rebirth_website/wasm/rbsWorklet.js`   |
| Build manifest | `public/wasm/wasm-build.json` | `/rebirth_website/wasm/wasm-build.json` |

`src/wasm/audio-module.config.ts` reads `import.meta.env.BASE_URL` (via `normalizeBase`) so these paths stay correct for the configured deployment base. The generated files remain ignored and are produced by CI rather than committed.

### AudioWorklet processor and node

The processor name (`"rbs-player"`) is set at runtime in C++ via
`WebAudioWorkletProcessorCreateOptions.name`. The node is created with zero
inputs and one stereo output (`outputChannelCounts = {2}`). JavaScript must not
register a second processor with the same name via `audioWorklet.addModule()`.
See [`cpp/worklet/RbsWorklet.cpp`](cpp/worklet/RbsWorklet.cpp).

### CI note

`npm run wasm:build` requires `emcc` on `PATH`. In GitHub Actions, install emsdk before the build step:

```yaml
- name: Install Emscripten
  uses: emscripten-core/setup-emsdk@v15
  with:
    version: 6.0.3
- name: Build shipping site
  run: npm run build:ship
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Browser (JavaScript / TypeScript)                                  │
│  ┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐   │
│  │ RbsPlayer.astro │   │ WasmAudioBridge │   │  UI callbacks   │   │
│  │   (UI shell)    │◄──│  (JS wrapper)   │◄──│  (position,    │   │
│  └─────────────────┘   └────────┬────────┘   │   status)       │   │
│                                 │            └─────────────────┘   │
│                                 │                                   │
│                                 ▼                                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Emscripten JS Glue  (public/wasm/rbsParser.js)             │   │
│  │  ├── loads .wasm binary                                     │   │
│  │  └── exposes C++ classes via embind                        │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                 │                                   │
│                                 ▼                                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  WASM Binary  (public/wasm/rbsParser.wasm)                  │   │
│  │  ├── RbsParser       (.rbs → ParsedSong)                    │   │
│  │  ├── RbsAudioEngine  (sequencer + mixer + voices)           │   │
│  │  └── RbsWorklet      (AudioWorkletProcessor callback)       │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                 │                                   │
│                                 ▼                                   │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Web Audio API                                              │   │
│  │  ├── AudioContext (main thread)                             │   │
│  │  └── AudioWorklet (dedicated audio thread, 128-frame blocks)│   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

## Directory Layout

```
src/wasm/
├── cpp/
│   ├── CMakeLists.txt           # Native + emcmake WASM (includes sources.cmake)
│   ├── sources.cmake            # Single source list for engine / parser / worklet
│   ├── Makefile                 # Optional g++ wrapper (no CMake required)
│   ├── main.cpp                 # Emscripten entry point + embind exports
│   ├── build.sh                 # Thin emcmake wrapper + worklet/manifest post-process
│   ├── tools/                   # rbs-inspect / rbm-inspect CLIs (JSON dumps)
│   ├── tests/                   # doctest unit tests (native only)
│   ├── native_stubs/            # Emscripten-only APIs stubbed for the native build
│   ├── third_party/doctest.h    # Vendored doctest 2.4.11
│   ├── parser/
│   │   ├── RbsParser.h/.cpp     # .rbs container + chunk decoding (HEAD/GLOB/DEVL/FX)
│   │   ├── RbsTrak.cpp          # TRAK/STRAK events + arrangement projection
│   │   ├── RbsMidiContainer.cpp # v1 / v1.5 MIDI-container songs
│   │   ├── RbsByteStream.h      # Internal: bounds-checked reader + chunk/VLQ helpers
│   │   ├── RbmParser.h/.cpp     # .rbm mod bundle parser
│   │   ├── ParsedSongJson.*     # ParsedSong → JSON (CLI + debugging)
│   │   ├── ParsedModJson.*      # ParsedMod → JSON (CLI + debugging)
│   │   ├── RbsTypes.h           # C++ structs matching .rbs data
│   │   └── RbsFormat.md         # Reverse-engineered format spec (RbmFormat.md too)
│   ├── audio/                   # WAV/AIFF sample decode + WAV writer (bounce)
│   ├── synth/
│   │   ├── Voice.h/.cpp         # Abstract voice base class
│   │   ├── Tb303Voice.h/.cpp    # TB-303 voice (ZDF ladder + PolyBLEP)
│   │   ├── Tr808Voice.h/.cpp    # TR-808 drums
│   │   ├── Tr909Voice.h/.cpp    # TR-909 drums
│   │   ├── DrumSynth.h/.cpp     # Shared procedural drum models
│   │   ├── SamplePool.h/.cpp    # .rbm sample arena (fixed, RT-safe)
│   │   └── dsp/                 # ZdfLadder.h, PolyBlep.h
│   ├── engine/
│   │   ├── RbsAudioEngine.h/.cpp# Top-level engine
│   │   ├── Sequencer.h/.cpp     # Pattern scheduler
│   │   ├── AutomationScheduler.*# TRAK automation playback
│   │   ├── EngineCommands.*     # Lock-free command queue + DeviceParamId
│   │   ├── EngineSnapshot.*     # RT-safe snapshot swap
│   │   └── Mixer.h/.cpp         # Stereo mix + FX
│   └── worklet/
│       └── RbsWorklet.cpp       # AudioWorkletProcessor callback (WASM only)
├── test-fixtures/               # Golden .rbs / .rbm files for native + browser tests
├── js/                          # TypeScript: everything from Embind out to the DOM
│   ├── WasmAudioBridge.ts       # Typed wrapper around the Emscripten module
│   ├── wasm-bounce.ts           # Offline bounce client (owns the render Worker)
│   ├── bounce-worker.ts         # Worker: second WASM instance, no AudioWorklet
│   ├── bounce-protocol.ts       # Message shapes shared with the Worker
│   ├── create-audio-context.ts  # AudioContext creation + lifecycle
│   ├── wasm-engine-io.ts        # Heap copies + engine error mapping
│   ├── wasm-locate-file.ts      # Emscripten locateFile remapping
│   ├── player-ui.ts             # Composer: state, file/demo loading
│   ├── player-dom.ts            # DOM lookup
│   ├── player-transport.ts      # Status, toasts, play/stop, volume, tempo
│   ├── player-studio-view.ts    # Pattern grid + device knobs
│   ├── player-studio.ts         # DeviceParam ids + knob mapping
│   ├── player-step-edit.ts      # Step patch model + inverse-patch undo stack
│   ├── player-test-hooks.ts     # window.* hooks for the Playwright specs
│   ├── rbs-init-errors.ts       # Init failure classification
│   ├── RbsMetadataSniffer.ts    # Pure-TS HEAD/GLOB/USRI sniffing (degraded mode)
│   └── DegradedRbsPlayer.ts     # Web Audio sketch preview (no WASM)
├── types/                       # One job per file; see CONTRACT.md
│   ├── wasm-audio-song.ts       # ParsedSong / devices / patterns (UI + Wasm shapes)
│   ├── wasm-audio-engine.ts     # Embind surface: EngineConfig, instances, bounce
│   ├── wasm-audio-config.ts     # WasmAudioModuleConfig + AudioContext diagnostics
│   ├── wasm-audio-mod.ts        # .rbm enums + load report + ParsedMod
│   └── wasm-audio-mapping.ts    # Wasm* → UI mapping helpers
├── audio-module.config.ts       # Runtime paths + feature flags
├── CONTRACT.md                  # C++ ↔ TS contract (SSOT, checked in CI)
└── README.md                    # This file
```

## The `.rbs` Binary Format

See [`cpp/parser/RbsFormat.md`](cpp/parser/RbsFormat.md) for the full reverse-engineered specification.

High-level structure:

1. **File header** — magic bytes `ReBirth Song File`, version, offset table
2. **Metadata** — title, author, info text, creator URL
3. **Device states** — 4 blocks (303-A, 303-B, 808, 909) with knob positions
4. **Patterns** — up to 32 per device, 1–16 steps each
5. **Arrangement** — ordered list of which pattern each device plays per bar
6. **Automation** — optional timestamped knob movements

## Contributing

If you have experience with:

- **Audio DSP** — TB-303 / TR-808 / TR-909 synthesis algorithms
- **Reverse engineering** — binary file format analysis
- **Emscripten / Web Audio** — WASM Audio Worklet optimisation

…please open a GitHub Discussion or PR. The parser and voices are implemented;
see the roadmap above for what is still open (`.rbm` skins, voice calibration,
an Embind-visible automation path).

## License

The WASM audio engine code is MIT licensed (same as the site).  
ReBirth RB-338 software is © Reason Studios / Propellerhead Software.
