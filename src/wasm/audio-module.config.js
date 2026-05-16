/**
 * src/wasm/audio-module.config.js
 *
 * Runtime configuration for the ReBirth RB-338 WebAssembly audio engine.
 *
 * ARCHITECTURE
 * ────────────
 *   .rbs file (ArrayBuffer)
 *        │
 *   RbsParser (WASM, C++) ──► ParsedSong struct
 *        │
 *   RbsAudioEngine (WASM, C++)
 *   ├── Sequencer ──► Event list per block
 *   ├── Tb303Voice ×2
 *   ├── Tr808Voice
 *   ├── Tr909Voice
 *   └── Mixer (stereo + FX)
 *        │
 *   Wasm Audio Worklet ──► Web Audio API ──► speakers
 *
 * BUILD
 * ─────
 *   cd src/wasm/cpp && ./build.sh
 *
 *   Requires: Emscripten (latest), bash
 *   Outputs : ../../../public/wasm/rbsParser.js
 *             ../../../public/wasm/rbsParser.wasm
 *             ../../../public/wasm/rbsWorklet.js
 */

/** @type {WasmAudioModuleConfig} */
export const wasmAudioConfig = {
  /** Path (from site root) of the compiled .wasm binary */
  wasmPath: '/wasm/rbsParser.wasm',

  /** Path of the Emscripten JS glue / bindings */
  glueScriptPath: '/wasm/rbsParser.js',

  /** Path of the AudioWorklet processor script */
  workletPath: '/wasm/rbsWorklet.js',

  /** Preferred AudioContext sample rate (Hz) */
  sampleRate: 44100,

  /** Render quantum size in frames (standard Web Audio = 128) */
  bufferSize: 128,

  /** Maximum TB-303 polyphony (hardware is monophonic = 1) */
  maxVoices: 1,

  /** Runtime feature flags — disable modules on low-power devices */
  features: {
    tb303_a:    true,
    tb303_b:    true,
    tr808:      true,
    tr909:      true,
    distortion: true,
    compressor: true,
    delay:      true,
  },
};

/**
 * @typedef {Object} WasmAudioModuleConfig
 * @property {string}  wasmPath        - Path to compiled .wasm binary
 * @property {string}  glueScriptPath  - Path to JS bindings
 * @property {string}  workletPath     - Path to AudioWorklet processor
 * @property {number}  sampleRate      - Target AudioContext sample rate
 * @property {number}  bufferSize      - Render quantum size in frames
 * @property {number}  maxVoices       - TB-303 polyphony limit
 * @property {Object}  features        - Per-module feature flags
 */
