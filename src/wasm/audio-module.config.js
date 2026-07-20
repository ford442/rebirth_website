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
 *   npm run wasm:build
 *   npm run wasm:build:debug
 *
 *   Requires: Emscripten 3.1.74, bash
 *   Outputs : public/wasm/rbsParser.js
 *             public/wasm/rbsParser.wasm
 *             public/wasm/rbsWorklet.js  (renamed from rbsParser.aw.js)
 *             public/wasm/wasm-build.json
 */

// Respect Astro/Vite's base path when the site is deployed under a subpath.
const BASE = (typeof import.meta !== 'undefined' && import.meta.env?.BASE_URL) || '/';
const BASE_SLASH = BASE.endsWith('/') ? BASE : `${BASE}/`;

/** @type {WasmAudioModuleConfig} */
export const wasmAudioConfig = {
  /** Path (from site root) of the compiled .wasm binary */
  wasmPath: `${BASE_SLASH}wasm/rbsParser.wasm`,

  /** Path of the Emscripten JS glue / bindings */
  glueScriptPath: `${BASE_SLASH}wasm/rbsParser.js`,

  /** Path of the AudioWorklet processor script */
  workletPath: `${BASE_SLASH}wasm/rbsWorklet.js`,

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

  /**
   * Master volume ownership
   * ───────────────────────
   * Playback volume (0–1) is applied inside WASM by RbsAudioEngine::setVolume()
   * after the Mixer renders each block. The JS GainNode in WasmAudioBridge stays
   * at unity (1.0) and exists only as a graph tap / future mute hook — do not
   * drive user-facing level from both layers.
   */
  masterVolumeOwner: 'wasm',
};

/**
 * @typedef {Object} EngineFeatures
 * @property {boolean} tb303_a    - Enable TB-303 voice A
 * @property {boolean} tb303_b    - Enable TB-303 voice B
 * @property {boolean} tr808      - Enable TR-808 drum machine
 * @property {boolean} tr909      - Enable TR-909 drum machine
 * @property {boolean} distortion - Enable distortion FX
 * @property {boolean} compressor - Enable compressor FX
 * @property {boolean} delay      - Enable delay FX
 */

/**
 * @typedef {Object} WasmAudioModuleConfig
 * @property {string}         wasmPath        - Path to compiled .wasm binary
 * @property {string}         glueScriptPath  - Path to JS bindings
 * @property {string}         workletPath     - Path to AudioWorklet processor
 * @property {number}         sampleRate      - Target AudioContext sample rate
 * @property {number}         bufferSize      - Render quantum size in frames
 * @property {number}         maxVoices       - TB-303 polyphony limit
 * @property {EngineFeatures} features        - Per-module feature flags
 * @property {'wasm'}         masterVolumeOwner - Where playback gain is applied
 */
