/**
 * Runtime configuration for the WASM audio module.
 *
 * These types describe what the *site* hands the engine at startup
 * (`src/wasm/audio-module.config.ts`) and what it learns back from the
 * browser's AudioContext. They never cross the Embind boundary — the flat
 * `EngineConfig` in `wasm-audio-engine.ts` does.
 */

/** Feature flags that can disable sub-devices at runtime.
 *
 *  This is a convenience shape used by `audio-module.config.ts`. The bridge
 *  flattens it into the flat `EngineConfig` fields before calling WASM.
 */
export interface EngineFeatures {
  tb303_a: boolean;
  tb303_b: boolean;
  tr808: boolean;
  tr909: boolean;
}

/**
 * Tempo range, mirroring `MIN_BPM` / `MAX_BPM` in
 * `src/wasm/cpp/parser/RbsTypes.h` (the contract check keeps them equal).
 * ReBirth documents 20–500 BPM; the parser, engine and UI all use it.
 */
export const TEMPO_MIN_BPM = 20;
export const TEMPO_MAX_BPM = 500;

/** Runtime paths and feature flags from `audio-module.config.ts`. */
export interface WasmAudioModuleConfig {
  wasmPath: string;
  glueScriptPath: string;
  workletPath: string;
  /**
   * Preferred live AudioContext sample rate (Hz), or null for the output
   * device's own rate. Null is the default: forcing 44.1 kHz on 48 kHz
   * hardware (most phones, Bluetooth, many DACs) makes the browser resample
   * every render quantum, at some CPU and latency cost. The engine takes its
   * rate from the context, so nothing in the DSP assumes 44.1 kHz.
   */
  preferredSampleRate: number | null;
  /** Sample rate of offline bounces (WAV / stems), where file compatibility matters. */
  offlineSampleRate: number;
  /** Latency hint for passive archive listening: larger buffers, fewer glitches. */
  latencyHint: AudioContextLatencyCategory;
  /** Latency hint once the studio, MIDI input or step editing is active. */
  interactiveLatencyHint: AudioContextLatencyCategory;
  features: EngineFeatures;
}

/** Runtime AudioContext metrics collected after init. */
export interface AudioContextDiagnostics {
  sampleRate: number;
  latencyHint: AudioContextLatencyCategory;
  /** Seconds; null when the property is unsupported. */
  baseLatency: number | null;
  /** Seconds; null when the property is unsupported. */
  outputLatency: number | null;
  /** Preferred rate passed to the constructor, or null when retry omitted it. */
  requestedSampleRate: number | null;
}
