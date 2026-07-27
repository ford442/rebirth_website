/**
 * Shared TypeScript interfaces for the ReBirth RB-338 WASM audio engine.
 *
 * This file contains two layers:
 *
 *   1. UI-facing types — the shape the Astro UI expects (ParsedSong, Pattern,
 *      DeviceState, etc.).
 *   2. WASM-facing types — the exact shape that crosses the Embind boundary
 *      from C++ (WasmParsedSong, WasmPattern, WasmDeviceState, etc.).
 *
 * The single source of truth for the C++ ↔ TypeScript contract is
 * `src/wasm/CONTRACT.md`. If you change a WASM-facing type here, update that
 * document and the matching C++ struct/Embid registration.
 */

/** Top-level status of the in-browser player UI */
export type PlayerStatus = 'idle' | 'loading' | 'ready' | 'playing' | 'error';

/** Result of parsing an .rbs file in WASM, formatted for the UI */
export interface ParsedSong {
  /** Song title (from .rbs metadata) */
  title: string;
  /** Author / creator (from .rbs metadata) */
  author: string;
  /** Tempo in BPM */
  bpm: number;
  /** Per-device knob positions, mutes, and global settings */
  devices: DeviceState[];
  /** All patterns defined in the song */
  patterns: Pattern[];
  /** Ordered list of bars — the song arrangement */
  arrangement: ArrangementStep[];
}

/** State snapshot for one of the four ReBirth devices (UI-facing) */
export interface DeviceState {
  deviceId: 'tb303-a' | 'tb303-b' | 'tr808' | 'tr909';
  /** Current knob values (key = knob name, value = 0.0–1.0 normalized) */
  knobs: Record<string, number>;
  /** Whether this device is muted in the mixer */
  muted: boolean;
}

/** A single pattern (up to 16 steps) for one device (UI-facing) */
export interface Pattern {
  /** Which device this pattern belongs to */
  deviceId: string;
  /** Bank index 0–3 (A–D) */
  bank: number;
  /** Pattern index 0–7 within the bank */
  patternIndex: number;
  /** Step data (length is 1–16 depending on pattern length setting) */
  steps: StepData[];
}

/** Data for one step in a pattern (UI-facing) */
export interface StepData {
  /** Is this step active (note on / drum hit)? */
  active: boolean;
  /** MIDI note number (for TB-303 only; undefined for drum machines) */
  note?: number;
  /** Is this step accented? */
  accent: boolean;
  /** Is this step slided? (TB-303 only) */
  slide: boolean;
}

/** One bar in the song arrangement (UI-facing) */
export interface ArrangementStep {
  /** Bar number (1-based, as shown in ReBirth) */
  bar: number;
  /** Which pattern each device plays at this bar */
  patternRefs: Record<string, PatternRef>;
}

/** Reference to a specific pattern by bank + index */
export interface PatternRef {
  bank: number;
  index: number;
}

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
  sampleRate: number;
  bufferSize: number;
  maxVoices: number;
  features: EngineFeatures;
}

/** Runtime engine configuration passed from JS to WASM on init.
 *
 *  Matches `EngineConfig` in `src/wasm/cpp/engine/RbsAudioEngine.h` exactly.
 */
export interface EngineConfig {
  /** Host AudioContext sample rate (Hz) */
  sampleRate: number;
  /** Render quantum size in frames (typically 128) */
  bufferSize: number;
  /** Enable TB-303 voice A */
  enableTb303A: boolean;
  /** Enable TB-303 voice B */
  enableTb303B: boolean;
  /** Enable TR-808 drum machine */
  enableTr808: boolean;
  /** Enable TR-909 drum machine */
  enableTr909: boolean;
  /** Enable distortion FX */
  enableDistortion: boolean;
  /** Enable compressor FX */
  enableCompressor: boolean;
  /** Enable delay FX */
  enableDelay: boolean;
}

/** Current playback position returned by the engine */
export interface PlaybackPosition {
  /** Current bar (1-based) */
  bar: number;
  /** Current step within the bar (0-based or 1-based depending on engine convention) */
  step: number;
}

/** Error info returned when parsing or playback fails */
export interface EngineError {
  code: string;
  message: string;
}

// ═══════════════════════════════════════════════════════════════════
// WASM-facing types — must match C++ structs field-for-field
// ═══════════════════════════════════════════════════════════════════

/** Numeric device IDs as encoded in C++ `DeviceId` */
export type WasmDeviceId = 0 | 1 | 2 | 3;

/** Step data as returned by the WASM parser */
export interface WasmStepData {
  active: boolean;
  note: number;
  drumExtra: number;
  accent: boolean;
  slide: boolean;
}

/** Pattern reference as encoded in C++ `PatternRef` */
export interface WasmPatternRef {
  bank: number;
  index: number;
}

/** One arrangement bar as encoded in C++ `ArrangementBar` */
export interface WasmArrangementBar {
  barNumber: number;
  devicePatterns: WasmPatternRef[];
}

/** Device state as encoded in C++ `DeviceState` */
export interface WasmDeviceState {
  id: WasmDeviceId;
  tune: number;
  cutoff: number;
  resonance: number;
  envMod: number;
  decay: number;
  accent: number;
  waveform: number;
  initialPatternBank: number;
  initialPatternIndex: number;
  muted: boolean;
  level: number;
  pan: number;
  dist: boolean;
  pcf: boolean;
  compressor: boolean;
  delaySend: number;
}

/** Pattern as encoded in C++ `Pattern` */
export interface WasmPattern {
  deviceId: WasmDeviceId;
  bank: number;
  patternIndex: number;
  length: number;
  steps: WasmStepData[];
}

/** Runtime handle produced by Embind's `register_vector<T>`. */
export interface EmbindVector<T> {
  size(): number;
  get(index: number): T;
  delete(): void;
}

/** Parsed song as encoded in C++ `ParsedSong` */
export interface WasmParsedSong {
  title: string;
  author: string;
  infoText: string;
  creatorUrl: string;
  bpm: number;
  version: number;
  headVersion: number;
  globSubFormat: number;
  showInfoOnOpen: boolean;
  devices: WasmDeviceState[];
  patterns: EmbindVector<WasmPattern>;
  arrangement: EmbindVector<WasmArrangementBar>;
}

// ═══════════════════════════════════════════════════════════════════
// Emscripten module interfaces
// ═══════════════════════════════════════════════════════════════════

/** Handle returned by `emscriptenRegisterAudioObject` */
export type AudioObjectHandle = number;

/** Instance handle returned by `initAudioWorklet` */
export type WorkletNodeHandle = number;

/** Class constructor shape exposed by Embind for `RbsAudioEngine` */
export interface RbsAudioEngineInstance {
  init(config: EngineConfig): boolean;
  loadSong(song: WasmParsedSong): boolean;
  play(): void;
  pause(): void;
  stop(): void;
  seek(bar: number): void;
  setVolume(volume: number): void;
  setTempo(bpm: number): void;
  getTempo(): number;
  setTempoMultiplier(multiplier: number): void;
  isPlaying(): boolean;
  getProcessedBlockCount(): number;
  renderTestBlock(numFrames: number): number;
  getPlaybackPosition(): PlaybackPosition;
  delete(): void;
}

/** Class constructor shape exposed by Embind for `RbsParser` */
export interface RbsParserInstance {
  /** Returns the parsed song, or `undefined` when parsing fails. */
  parse(ptr: number, size: number): WasmParsedSong | undefined;
  lastError(): string;
  delete(): void;
}

/** Constructor signature returned by Embind */
export type EmbindClassConstructor<T> = new () => T;

/** The instantiated Emscripten module returned by the JS glue */
export interface EngineModule {
  /** Embind exports are installed directly on the module object. */
  DeviceId: Record<string, WasmDeviceId>;
  RbsAudioEngine: EmbindClassConstructor<RbsAudioEngineInstance>;
  RbsParser: EmbindClassConstructor<RbsParserInstance>;
  initAudioWorklet(
    contextHandle: AudioObjectHandle,
    engine: RbsAudioEngineInstance,
    callback: (nodeHandle: WorkletNodeHandle | null) => void
  ): void;

  /** Heap views */
  HEAPU8: Uint8Array;

  /** Manual memory management (exported via `-sEXPORTED_FUNCTIONS`) */
  _malloc(size: number): number;
  _free(ptr: number): void;

  /** Audio object registry (exported via `-sEXPORTED_RUNTIME_METHODS`) */
  emscriptenRegisterAudioObject(obj: AudioContext): AudioObjectHandle;
  emscriptenGetAudioObject<T = unknown>(handle: AudioObjectHandle): T;
}
