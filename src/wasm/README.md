# WebAssembly Audio Module — `src/wasm/`

This directory is the future home of the ReBirth RB-338 WebAssembly audio engine, which
will enable in-browser parsing and real-time playback of `.rbs` song files.

## Status

> **⚠ PENDING IMPLEMENTATION**  
> No compiled WASM binaries exist here yet. The directory and configuration stubs are
> placeholders to establish the planned architecture.

## Planned Architecture

```
.rbs file (binary)
      │
rbsParser.wasm  ──► JS glue bindings (rbsParser.js)
      │
PatternData  ──► AudioWorkletProcessor (WASM-backed)
                       │
               Web Audio API ──► browser audio output
```

## Planned Files

| File                      | Description                                                  |
|---------------------------|--------------------------------------------------------------|
| `rbsParser.wasm`          | Compiled binary — RBS file parser + synthesis engine        |
| `rbsParser.js`            | Emscripten/wasm-pack JS glue / bindings                     |
| `rbsWorkletProcessor.js`  | `AudioWorkletProcessor` subclass, runs in audio thread      |
| `audio-module.config.js`  | Runtime configuration object (already present)              |

## Build Toolchain (planned)

| Option          | Language | Compiler   | Command              |
|-----------------|----------|------------|----------------------|
| Emscripten path | C / C++  | Emscripten | `emcc src/rb338.c …` |
| wasm-pack path  | Rust     | wasm-pack  | `wasm-pack build`    |

Compiled artefacts should be placed in **`public/wasm/`** so Astro serves them at `/wasm/`.

## Contributing

If you have knowledge of the `.rbs` binary format or experience with audio DSP in
WebAssembly, contributions to this module are very welcome. Please open a GitHub
Discussion or PR in the [rebirth_website](https://github.com/ford442/rebirth_website)
repository.

## Format Reference

The `.rbs` binary format (community-documented) broadly consists of:

1. **File header** — magic bytes `ReBirth Song File`, format version, metadata length
2. **Device state blocks** — one block per device (303-A, 303-B, 808, 909), each
   containing initial knob positions and global settings
3. **Pattern blocks** — up to 64 patterns per device, each with a 16-step grid of
   note/rest, accent, and slide data
4. **Song arrangement** — an ordered list of pattern indices per device (the "song")
5. **Automation data** — timestamped knob-movement records (optional)

A full reverse-engineered specification will be added to `src/content/docs/` once the
parser reaches a working state.
