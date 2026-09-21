/**
 * Engine-facing types: the Embind surface of the WASM module.
 *
 * Everything here describes a handle, a call signature or a struct that
 * actually crosses into C++ — `EngineConfig`, the `RbsAudioEngine` /
 * `RbsParser` / `RbmParser` instance shapes, the Emscripten module object,
 * and the offline-bounce helpers that drive `renderOfflineToWav`.
 *
 * The single source of truth for the C++ <-> TypeScript contract is
 * `src/wasm/CONTRACT.md`. Run `npm run contract:check` after changing either
 * side.
 */

import type { DeviceId, WasmDeviceId, WasmParsedSong, WasmStepData } from './wasm-audio-song';
import type { WasmModLoadReport } from './wasm-audio-mod';

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

/** Runtime handle produced by Embind's `register_vector<T>`. */
export interface EmbindVector<T> {
  size(): number;
  get(index: number): T;
  delete(): void;
}

// ═══════════════════════════════════════════════════════════════════
// Offline bounce
// ═══════════════════════════════════════════════════════════════════

/** `deviceIndex` value that renders the full mix rather than a stem. */
export const MASTER_BUS = 255;

/** Device order used by stem exports, matching C++ `DeviceId`. */
export const STEM_DEVICES: ReadonlyArray<{ index: number; id: DeviceId; slug: string }> = [
  { index: 0, id: 'tb303-a', slug: '303a' },
  { index: 1, id: 'tb303-b', slug: '303b' },
  { index: 2, id: 'tr808', slug: '808' },
  { index: 3, id: 'tr909', slug: '909' },
];

/** One rendered file ready to hand to the browser as a download. */
export interface RenderedFile {
  filename: string;
  bytes: Uint8Array;
  mimeType: string;
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
  /**
   * In-process writer/test path. Archive loads must use loadSongFromBytes so
   * TRAK automation is not dropped by the Embind copy.
   */
  loadSong(song: WasmParsedSong): boolean;
  /** Parse + load from WASM heap bytes. Returns the UI summary, or undefined. */
  loadSongFromBytes(ptr: number, size: number): WasmParsedSong | undefined;
  lastParseError(): string;
  /** Decode a `.rbm` already copied into the WASM heap. Returns ModLoadStatus. */
  loadMod(ptr: number, size: number): number;
  clearMod(): void;
  hasMod(): boolean;
  getModReport(): WasmModLoadReport;
  play(): void;
  pause(): void;
  stop(): void;
  seek(bar: number): void;
  setVolume(volume: number): void;
  setTempo(bpm: number): void;
  getTempo(): number;
  setTempoMultiplier(multiplier: number): void;
  setDeviceParam(deviceId: number, paramId: number, value: number): void;
  /**
   * Pattern edit, engine working copy. One step patch per call — never a
   * whole `WasmParsedSong`, which cannot carry TRAK automation back across
   * Embind. Session-only: the loaded `.rbs` bytes are not rewritten.
   */
  setStep(
    deviceId: number,
    bank: number,
    patternIndex: number,
    stepIndex: number,
    step: WasmStepData
  ): boolean;
  /** Read a step back; an all-false `WasmStepData` for a slot the song lacks. */
  getStep(deviceId: number, bank: number, patternIndex: number, stepIndex: number): WasmStepData;
  setPatternLength(
    deviceId: number,
    bank: number,
    patternIndex: number,
    length: number
  ): boolean;
  /** Pattern play length, or 0 when the song has no such pattern. */
  getPatternLength(deviceId: number, bank: number, patternIndex: number): number;
  isPlaying(): boolean;
  getProcessedBlockCount(): number;
  renderTestBlock(numFrames: number): number;
  /**
   * Bounce offline to a 16-bit PCM WAV. `deviceIndex` 0-3 renders a stem;
   * MASTER_BUS (255) renders the full mix. Returns a JS-owned Uint8Array.
   */
  renderOfflineToWav(frames: number, deviceIndex: number): Uint8Array;
  /** Frames covering the loaded arrangement at the current tempo. */
  songLengthFrames(): number;
  getPlaybackPosition(): PlaybackPosition;
  delete(): void;
}

/** Allocator snapshot from Embind `heapStats()`. */
export interface HeapStats {
  initialMemory: number;
  heapSize: number;
  usedBytes: number;
}

/** Class constructor shape exposed by Embind for `RbsParser` */
export interface RbsParserInstance {
  /** Returns the parsed song, or `undefined` when parsing fails. */
  parse(ptr: number, size: number): WasmParsedSong | undefined;
  lastError(): string;
  delete(): void;
}

/**
 * Class constructor shape exposed by Embind for `RbmParser`.
 *
 * Metadata only — it reads sample headers but decodes no PCM and returns no
 * payload bytes. Use `RbsAudioEngine.loadMod()` to actually load samples.
 */
export interface RbmParserInstance {
  /** Returns a mod summary, or `undefined` when parsing fails. */
  parse(ptr: number, size: number): WasmModLoadReport | undefined;
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
  RbmParser: EmbindClassConstructor<RbmParserInstance>;
  initAudioWorklet(
    contextHandle: AudioObjectHandle,
    engine: RbsAudioEngineInstance,
    callback: (nodeHandle: WorkletNodeHandle | null) => void
  ): void;
  heapStats(): HeapStats;

  /** Heap views */
  HEAPU8: Uint8Array;

  /** Manual memory management (exported via `-sEXPORTED_FUNCTIONS`) */
  _malloc(size: number): number;
  _free(ptr: number): void;

  /** Audio object registry (exported via `-sEXPORTED_RUNTIME_METHODS`) */
  emscriptenRegisterAudioObject(obj: AudioContext): AudioObjectHandle;
  emscriptenGetAudioObject<T = unknown>(handle: AudioObjectHandle): T;
}
