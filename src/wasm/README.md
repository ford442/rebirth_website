# WebAssembly Audio Module — `src/wasm/`

In-browser playback engine for ReBirth RB-338 `.rbs` song files.

## Status

> **⚠️ PENDING IMPLEMENTATION**  
> C++ source files are scaffolded with TODO stubs. No compiled WASM binaries exist yet.  
> The UI component (`RbsPlayer.astro`) is functional and will show a graceful fallback on unsupported browsers.

## Quick Start

```bash
# 1. Install Emscripten (one-time)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# 2. Build
cd src/wasm/cpp
./build.sh

# 3. Verify outputs exist
ls ../../../public/wasm/
# → rbsParser.js  rbsParser.wasm  rbsWorklet.js
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
