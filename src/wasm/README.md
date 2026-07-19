# WebAssembly Audio Module — `src/wasm/`

In-browser playback engine for ReBirth RB-338 `.rbs` song files.

## Status

> **⚠️ PARTIAL IMPLEMENTATION**  
> Parser, sequencer, transport, and **Phase 1 synthetic TR-808 / TR-909 drums** are implemented in C++.  
> No compiled WASM binaries ship from CI yet — run `npm run wasm:build` locally with Emscripten.  
> The UI component (`RbsPlayer.astro`) degrades gracefully when WASM is missing: metadata sniffing and sketch preview remain available.

## Player capability matrix

| Capability | WASM engine | Degraded (sketch) | Degraded (metadata only) |
|------------|-------------|-------------------|--------------------------|
| Load `.rbs` via drag/drop or file picker | ✅ | ✅ | ✅ |
| Show title / author from file header | ✅ | ✅ | ✅ |
| Full ReBirth synthesis (303/808/909) | ✅ | ❌ | ❌ |
| Transport play / pause / stop | ✅ | ✅ (metronome sketch) | ❌ (play disabled) |
| Step bar + level meter animation | ✅ | ✅ | ❌ |
| Tempo slider | ✅ | ✅ | ✅ (display only) |
| Volume slider | ✅ | ✅ | ❌ |

### Init failure reasons

When `WasmAudioBridge.init()` fails, `RbsPlayer` classifies the error and switches to `DegradedRbsPlayer`:

| Reason | Typical cause | User message |
|--------|---------------|--------------|
| `unsupported-browser` | No WebAssembly | Browser lacks required APIs |
| `wasm-unavailable` | `public/wasm/` empty (not built) | WASM binaries not built yet |
| `wasm-load-failed` | 404 / blocked glue script | WASM assets failed to load |
| `worklet-unavailable` | No `AudioWorkletNode` | AudioWorklet not supported |
| `worklet-init-failed` | Worklet registration error | Worklet init failed |
| `engine-init-failed` | Other engine errors | Generic degraded fallback |

Pure-TS metadata parsing lives in `src/wasm/js/RbsMetadataSniffer.ts` (HEAD / GLOB / USRI chunks). Sketch preview uses Web Audio oscillators in `src/wasm/js/DegradedRbsPlayer.ts` — audible metronome clicks, not silent success.


## Integration Roadmap (Phase Plan)

1. **Parser completion** — metadata and patterns decode from real `.rbs` payloads ✅. Arrangement (`TRAK` chunks) is pending.
2. **Audio engine parity** — Phase 1 procedural TR-808 / TR-909 drums (BD, SD, CH, OH, RS, CP / clap) ✅. TB-303 filter/slide DSP and `.rbm` sample playback remain future work.
3. **Realtime control API** — transport + tempo + volume commands flow through a lock-free queue ✅.
4. **Archive demo pipeline** — add curated demo `.rbs` files under `public/archive/rbs-songs/demo/` for direct browser previews.
5. **Fallback mode** — if WASM init fails, provide metadata sniffing + Web Audio sketch preview so the UI remains usable ✅.
6. **End-to-end validation** — add browser tests that cover upload, demo loading, transport controls, and fallback behaviour ✅ (degraded path in `tests/rbs-player.spec.ts`).

## Audio thread architecture

We use **Architecture A — Emscripten Wasm Audio Worklet**.

- **Main thread** (`WasmAudioBridge`):
  - Loads the Emscripten glue and WASM module.
  - Creates the `AudioContext` and wires the `AudioWorkletNode`.
  - Parses `.rbs` files via `RbsParser` and calls `RbsAudioEngine::loadSong()`.
  - Sends transport/control commands (`play`, `pause`, `stop`, `seek`, `setVolume`, `setTempo`) by pushing them into a lock-free command queue that lives in shared WASM memory.
  - Polls `getPlaybackPosition()` for UI updates.

- **Audio thread** (`RbsWorklet` / `RbsAudioEngine::processBlock()`):
  - Owns the `Sequencer`, `Mixer`, and all `Voice` instances.
  - Drains the command queue at the start of every 128-frame render quantum.
  - Generates sample-accurate step events from the current BPM and sample rate.
  - Renders each voice into a scratch mono buffer and mixes to interleaved stereo.
  - Publishes `bar`/`step` via atomic variables so the main thread can read them without locking.

This keeps latency low (128-frame Web Audio quanta), avoids main-thread synthesis work, and matches the existing `-sAUDIO_WORKLET=1` / `-sWASM_WORKERS=1` build configuration.

### Why not Option B or C?

- **Option B** (main-thread WASM + ScriptProcessor/ring buffer) would push synthesis or PCM streaming onto the main thread, creating latency and jank risks for a UI-heavy archive site.
- **Option C** (hybrid: parse on main, synthesise on audio thread) is essentially the same runtime shape as Option A; we capture it explicitly above by stating that `.rbs` parsing stays on the main thread while the audio thread owns synthesis and sequencing.

### Real-time constraints

The audio callback (`processBlock()`) must never:

- allocate heap memory (`malloc` / `new` / `std::vector` resize),
- take a mutex or spin lock,
- call into JS,
- or perform file I/O.

All control data is pre-allocated or passed through the lock-free `EngineCommandQueue`. Position is shared via `std::atomic`. Scratch render buffers are fixed-size stack arrays.

## Quick Start

```bash
# 1. Install Emscripten (one-time)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 3.1.74
./emsdk activate 3.1.74
source ./emsdk_env.sh

# 2. Build (release)
npm run wasm:build

# 3. Build (debug)
npm run wasm:build:debug

# 4. Verify outputs exist
ls public/wasm/
# → rbsParser.js  rbsParser.wasm  rbsWorklet.js  wasm-build.json
```

The pinned Emscripten version lives in [`cpp/.emscripten-version`](cpp/.emscripten-version). Update it deliberately when you need a newer toolchain; the build script warns when the installed version does not match.

## Build Profiles

The build script (`src/wasm/cpp/build.sh`) supports two modes:

| Mode | Command | Optimisation | Memory growth | Best for |
|------|---------|--------------|---------------|----------|
| Release | `npm run wasm:build` | `-O3 -flto` | Disabled, fixed 64 MB heap | Shipping |
| Debug | `npm run wasm:build:debug` | `-O0 -g3` | Enabled, 32–128 MB cap | Development |

Release uses a fixed heap so the audio callback never triggers a memory resize. Debug enables `ASSERTIONS`, `SAFE_HEAP`, `STACK_OVERFLOW_CHECK`, and `WEBAUDIO_DEBUG` to catch memory and worklet issues early.

### Emitted files

Emscripten produces `rbsParser.js`, `rbsParser.wasm`, and `rbsParser.aw.js`. The build script renames `rbsParser.aw.js` to `rbsWorklet.js` so the rest of the project refers to a stable filename. A `wasm-build.json` manifest is also generated for cache-busting and diagnostics.

### Deployment paths

Assets live in `public/wasm/` and are served from the site's base path. With `base: '/rb338'` (the GitHub Pages/SFTP deployment), the runtime URLs become:

| File | Public | Served at |
|------|--------|-----------|
| Glue | `public/wasm/rbsParser.js` | `/rb338/wasm/rbsParser.js` |
| WASM | `public/wasm/rbsParser.wasm` | `/rb338/wasm/rbsParser.wasm` |
| Worklet | `public/wasm/rbsWorklet.js` | `/rb338/wasm/rbsWorklet.js` |

`src/wasm/audio-module.config.js` reads `import.meta.env.BASE_URL` so these paths stay correct in both dev (`/`) and production (`/rb338`).

### AudioWorklet processor name

The processor name (`"rbs-player"`) is **not** a linker flag. It is set at runtime in C++ via `WebAudioWorkletProcessorCreateOptions.name` when calling `emscripten_create_wasm_audio_worklet_processor_async()`. See [`cpp/worklet/RbsWorklet.cpp`](cpp/worklet/RbsWorklet.cpp) for the registration code.

### CI note

`npm run wasm:build` requires `emcc` on `PATH`. In GitHub Actions, install emsdk before the build step:

```yaml
- name: Install Emscripten
  uses: mymindstorm/setup-emsdk@v14
  with:
    version: 3.1.74
- name: Build WASM
  run: npm run wasm:build
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
│   ├── main.cpp                 # Emscripten entry point + embind exports
│   ├── build.sh                 # Emscripten compile script
│   ├── parser/
│   │   ├── RbsParser.h          # .rbs binary parser interface
│   │   ├── RbsParser.cpp        # Parser implementation (stub)
│   │   ├── RbsTypes.h           # C++ structs matching .rbs data
│   │   └── RbsFormat.md         # Reverse-engineered format spec
│   ├── synth/
│   │   ├── Voice.h/.cpp         # Abstract voice base class
│   │   ├── Tb303Voice.h/.cpp    # TB-303 emulation (stub)
│   │   ├── Tr808Voice.h/.cpp    # TR-808 drums (stub)
│   │   └── Tr909Voice.h/.cpp    # TR-909 drums (stub)
│   ├── engine/
│   │   ├── RbsAudioEngine.h/.cpp# Top-level engine (stub)
│   │   ├── Sequencer.h/.cpp     # Pattern scheduler (stub)
│   │   └── Mixer.h/.cpp         # Stereo mix + FX (stub)
│   └── worklet/
│       └── RbsWorklet.cpp       # AudioWorkletProcessor callback
├── js/
│   └── WasmAudioBridge.ts       # Typed JS wrapper around Emscripten Module
├── types/
│   └── wasm-audio.ts            # Shared TypeScript interfaces
├── audio-module.config.js       # Runtime paths + feature flags
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

…please open a GitHub Discussion or PR. The parser and voice stubs are ready to be filled in.

## License

The WASM audio engine code is MIT licensed (same as the site).  
ReBirth RB-338 software is © Reason Studios / Propellerhead Software.
