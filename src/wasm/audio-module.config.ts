/**
 * Runtime configuration for the ReBirth RB-338 WebAssembly audio engine.
 *
 * See `src/wasm/README.md` for architecture and build instructions.
 */

import { normalizeBase } from '../lib/url';
import type { WasmAudioModuleConfig } from './types/wasm-audio-config';

const base = normalizeBase(import.meta.env.BASE_URL);
const prefix = base ? `${base}/` : '/';

export const wasmAudioConfig: WasmAudioModuleConfig = {
  /** Path (from site root) of the compiled .wasm binary */
  wasmPath: `${prefix}wasm/rbsParser.wasm`,

  /** Path of the Emscripten JS glue / bindings */
  glueScriptPath: `${prefix}wasm/rbsParser.js`,

  /** Path of the AudioWorklet processor script */
  workletPath: `${prefix}wasm/rbsWorklet.js`,

  /** Live AudioContext rate: null = the output device's own rate (no resampling) */
  preferredSampleRate: null,

  /** Offline bounces stay at CD rate, where file compatibility matters */
  offlineSampleRate: 44100,

  /** Passive archive listening: larger buffers, fewer glitches on low-end devices */
  latencyHint: 'playback',

  /** Studio, MIDI input or step editing: the context is rebuilt with this hint */
  interactiveLatencyHint: 'interactive',

  /** Runtime feature flags — disable devices on low-power hardware */
  features: {
    tb303_a: true,
    tb303_b: true,
    tr808: true,
    tr909: true,
  },
};
