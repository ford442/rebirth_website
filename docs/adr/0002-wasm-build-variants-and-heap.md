# ADR 0002 — WASM build variants and heap policy

- **Status:** Accepted
- **Date:** 2026-08-30
- **Deciders:** ReBirth RB-338 archive maintainers

## Context

The shipping audio engine uses Emscripten pthread AudioWorklet (`-pthread
-sWASM_WORKERS=1 -sAUDIO_WORKLET=1`) with a **fixed 64 MiB** WASM heap. GitHub
Pages relies on `coi-serviceworker.js` (via `dist/sw.js`) to synthesize
COOP/COEP so `SharedArrayBuffer` is available.

Before adding `.rbm` sample banks, offline bounce, or a second build flavour,
linker flags, stack sizes, and heap policy must be explicit and documented.

## WASM build variants

| Variant | Linker profile | Host requirement | Use case |
| ------- | -------------- | ---------------- | -------- |
| **pthread** (shipping) | `-pthread -sWASM_WORKERS=1 -sAUDIO_WORKLET=1` | `crossOriginIsolated === true` | Real-time archive preview |
| **fallback** | Not shipped in v1 | No SAB / no COI SW | Degraded JS player only |

### Non-pthread AudioWorklet spike (Emscripten 6.0.3)

**Hypothesis:** `-sAUDIO_WORKLET=1` without `-pthread`/`-sWASM_WORKERS=1` could
provide a COI-free real-time path.

**Result:** Not viable for this codebase. Emscripten's Wasm Audio Worklet API
(`emscripten_start_wasm_audio_worklet_thread_async`, shared stack in linear
memory) is built on pthread workers and `SharedArrayBuffer`. Removing pthread
flags breaks worklet thread startup and Embind engine sharing across threads.

**Decision:** Do not ship a second WASM binary in v1. When COI is unavailable:

1. `WasmAudioBridge.init()` fails with `not-cross-origin-isolated`.
2. The UI falls back to [`DegradedRbsPlayer`](../../src/wasm/js/DegradedRbsPlayer.ts)
   (metadata + metronome sketch).

Revisit if Emscripten adds a single-threaded AudioWorklet path or if we add
main-thread offline `renderTestBlock`-only preview without a worklet.

## Heap policy

### Shipping audio binary (current)

| Setting | Release | Debug |
| ------- | ------- | ----- |
| `INITIAL_MEMORY` | 64 MiB (`67108864`) | 32 MiB |
| `ALLOW_MEMORY_GROWTH` | `0` (disabled) | `1` (cap 128 MiB) |
| `STACK_SIZE` (module linear stack) | 128 KiB | 128 KiB |
| AudioWorklet pthread stack | 64 KiB (`AUDIO_THREAD_STACK_SIZE`) | 64 KiB |

Release keeps growth disabled so the audio callback never triggers a heap resize.

### Future `.rbm` sample banks

Multi-megabyte sample pools will not fit comfortably in 64 MiB alongside parser
state, engine voices, and delay lines.

**Options (choose before wiring mod playback):**

1. **Bump `INITIAL_MEMORY`** on the shipping binary after benchmarking peak
   usage with representative mods (preferred if headroom is modest).
2. **Separate "studio" build** with higher fixed heap (e.g. 128 MiB) for
   editors; keep the archive player lean.
3. **Main-thread allocation:** load/decode samples on the main thread; pass
   offsets into WASM; never `malloc` in `processBlock`.

**Rejected without benchmark:** `-sALLOW_MEMORY_GROWTH=1` on the shipping audio
binary — growth can stall the real-time thread.

## `-fno-exceptions` spike

CMake option `RB338_NO_EXCEPTIONS` compiles `rb338_engine` with
`-fno-exceptions`. Embind glue and the `rbsParser` executable target still use
exceptions. Use only in native test matrix to measure; do not enable for the
shipping Emscripten link until Embind compatibility is proven.

## Consequences

- Linker flags live in [`src/wasm/cpp/CMakeLists.txt`](../../src/wasm/cpp/CMakeLists.txt)
  (single source of truth); `build.sh` is an `emcmake` wrapper only.
- JS hosts must use `createProductionAudioContext()` (`latencyHint: 'interactive'`,
  sample-rate retry) — see [`src/wasm/js/create-audio-context.ts`](../../src/wasm/js/create-audio-context.ts).
- Heap size stays at 64 MiB until mod playback benchmarking dictates otherwise.
