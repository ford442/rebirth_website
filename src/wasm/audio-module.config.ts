/**
 * Runtime configuration for the ReBirth RB-338 WebAssembly audio engine.
 *
 * See `src/wasm/README.md` for architecture and build instructions.
 */

import { normalizeBase } from '../lib/url';
import type { WasmAudioModuleConfig } from './types/wasm-audio';

const base = normalizeBase(import.meta.env.BASE_URL);
const prefix = base ? `${base}/` : '/';

export const wasmAudioConfig: WasmAudioModuleConfig = {
  /** Path (from site root) of the compiled .wasm binary */
  wasmPath: `${prefix}wasm/rbsParser.wasm`,

  /** Path of the Emscripten JS glue / bindings */
  glueScriptPath: `${prefix}wasm/rbsParser.js`,

  /** Path of the AudioWorklet processor script */
  workletPath: `${prefix}wasm/rbsWorklet.js`,

  /** Preferred AudioContext sample rate (Hz) */
  preferredSampleRate: 44100,
  sampleRate: 44100,

  /** Request low output latency for interactive archive preview */
  latencyHint: 'interactive',

  /** Render quantum size in frames (standard Web Audio = 128) */
  bufferSize: 128,

  /** Maximum TB-303 polyphony (hardware is monophonic = 1) */
  maxVoices: 1,

  /** Runtime feature flags — disable modules on low-power devices */
  features: {
    tb303_a: true,
    tb303_b: true,
    tr808: true,
    tr909: true,
    distortion: true,
    compressor: true,
    delay: true,
  },
};
