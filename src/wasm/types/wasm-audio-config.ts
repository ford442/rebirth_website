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
  distortion: boolean;
  compressor: boolean;
  delay: boolean;
}

/** Runtime paths and feature flags from `audio-module.config.ts`. */
export interface WasmAudioModuleConfig {
  wasmPath: string;
  glueScriptPath: string;
  workletPath: string;
  /** Preferred AudioContext sample rate (Hz); browser may use a different rate. */
  preferredSampleRate: number;
  /** AudioContext latency hint for archive preview */
  latencyHint: AudioContextLatencyCategory;
  bufferSize: number;
  maxVoices: number;
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
