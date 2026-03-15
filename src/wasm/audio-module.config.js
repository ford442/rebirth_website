/**
 * src/wasm/audio-module.config.js
 *
 * Configuration stub for the future ReBirth RB-338 WebAssembly audio engine.
 *
 * PLANNED FUNCTIONALITY
 * ─────────────────────
 * This module will expose a WebAssembly build of an `.rbs` file parser and
 * real-time audio synthesis engine, allowing users to preview ReBirth song
 * files directly in the browser via the Web Audio API.
 *
 * ARCHITECTURE (target)
 * ──────────────────────
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  .rbs file (binary)                                         │
 *   │      │                                                       │
 *   │  rbsParser.wasm  ──► JS bindings (rbsParser.js)             │
 *   │      │                                                       │
 *   │  PatternData  ──► AudioWorkletProcessor (wasm-backed)       │
 *   │                         │                                   │
 *   │                   Web Audio API ──► <audio> output          │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * BUILD TOOLCHAIN (planned)
 * ─────────────────────────
 *   Source language : C or Rust
 *   Compiler        : Emscripten (C) or wasm-pack (Rust)
 *   Output          : rbsParser.wasm + rbsParser.js glue
 *   Integration     : Astro vite plugin or manual public/ placement
 *
 * USAGE (future API, subject to change)
 * ──────────────────────────────────────
 *   import { loadRbsParser } from '../wasm/rbsParser.js';
 *
 *   const parser = await loadRbsParser();
 *   const song   = await parser.parse(arrayBuffer);   // ArrayBuffer of .rbs file
 *   const player = parser.createPlayer(song);
 *   player.connect(audioContext.destination);
 *   player.start();
 */

/** @type {WasmAudioModuleConfig} */
export const wasmAudioConfig = {
  /** Relative path (from /public) where the compiled .wasm binary will be served */
  wasmPath: '/wasm/rbsParser.wasm',

  /** Relative path (from /public) of the JS glue/bindings emitted by Emscripten/wasm-pack */
  glueScriptPath: '/wasm/rbsParser.js',

  /**
   * AudioWorklet processor script path.
   * The worklet is responsible for running the synthesis loop in a dedicated
   * audio-rendering thread, keeping the main thread free of jank.
   */
  workletPath: '/wasm/rbsWorkletProcessor.js',

  /** Preferred sample rate for the AudioContext (Hz) */
  sampleRate: 44100,

  /** Buffer size (frames) handed to the AudioWorklet per render quantum */
  bufferSize: 128,

  /** Maximum polyphony for the TB-303 emulation (hardware is monophonic = 1) */
  maxVoices: 1,

  /**
   * Feature flags — enable or disable sub-modules at runtime.
   * Useful for progressive enhancement on low-power devices.
   */
  features: {
    tb303_a:    true,   // First TB-303 emulation channel
    tb303_b:    true,   // Second TB-303 emulation channel
    tr808:      true,   // TR-808 drum machine
    tr909:      true,   // TR-909 drum machine
    distortion: true,   // Master distortion effect
    compressor: true,   // Master compressor
    delay:      true,   // Pattern delay
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
