/**
 * Engine-facing types: the Embind surface of the WASM module.
 *
 * Everything here describes a handle, a call signature or a struct that
 * actually crosses into C++ — `EngineConfig`, the `RbsAudioEngine` /
 * `RbsParser` / `RbmParser` instance shapes, the Emscripten module object,
 * and the offline-bounce helpers that drive the chunked `renderOfflineChunk`.
 *
 * The single source of truth for the C++ <-> TypeScript contract is
 * `src/wasm/CONTRACT.md`. Run `npm run contract:check` after changing either
 * side.
 */

import type {
  DeviceId,
  WasmArrangementBar,
  WasmDeviceId,
  WasmPattern,
  WasmSongSummary,
  WasmStepData,
} from './wasm-audio-song';
import type { WasmModLoadReport } from './wasm-audio-mod';

/** Runtime engine configuration passed from JS to WASM on init.
 *
 *  Matches `EngineConfig` in `src/wasm/cpp/engine/RbsAudioEngine.h` exactly.
 */
export interface EngineConfig {
  /** Host AudioContext sample rate (Hz); the offline bounce uses its own. */
  sampleRate: number;
  /** Enable TB-303 voice A */
  enableTb303A: boolean;
  /** Enable TB-303 voice B */
  enableTb303B: boolean;
  /** Enable TR-808 drum machine */
  enableTr808: boolean;
  /** Enable TR-909 drum machine */
  enableTr909: boolean;
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

/** Mirrors `RbsAudioEngine::OFFLINE_CHUNK_FRAMES`: frames per bounce chunk. */
export const OFFLINE_CHUNK_FRAMES = 1 << 16;

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
   * Parse + load from WASM heap bytes. Returns the slim summary, or
   * undefined (see `lastParseError()`). Patterns and the arrangement are
   * fetched on demand with `getPattern` / `getArrangement`.
   */
  loadSongFromBytes(ptr: number, size: number): WasmSongSummary | undefined;
  lastParseError(): string;
  /** Decode a `.rbm` already copied into the WASM heap. Returns ModLoadStatus. */
  loadMod(ptr: number, size: number): number;
  clearMod(): void;
  hasMod(): boolean;
  getModReport(): WasmModLoadReport;
  /**
   * Transport commands are queued for the audio thread. Each returns false
   * when the 64-entry queue is full — only possible while the audio callback
   * is not running (a suspended AudioContext) — so the caller can retry.
   * Volume, tempo and device knobs never use the queue.
   */
  play(): boolean;
  pause(): boolean;
  stop(): boolean;
  seek(bar: number): boolean;
  setVolume(volume: number): void;
  setTempo(bpm: number): void;
  getTempo(): number;
  setTempoMultiplier(multiplier: number): void;
  setDeviceParam(deviceId: number, paramId: number, value: number): void;
  /**
   * Pattern edit, engine working copy. One step patch per call — never a
   * whole song, which could not carry TRAK automation back across
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
  setPatternLength(deviceId: number, bank: number, patternIndex: number, length: number): boolean;
  /** Pattern play length, or 0 when the song has no such pattern. */
  getPatternLength(deviceId: number, bank: number, patternIndex: number): number;
  /** One pattern of the working copy (edits included), or undefined for an unstored slot. */
  getPattern(deviceId: number, bank: number, patternIndex: number): WasmPattern | undefined;
  /** The arrangement, as an Embind vector the caller must `delete()`. */
  getArrangement(): EmbindVector<WasmArrangementBar>;
  /**
   * Serialise the engine's working copy back to `.rbs` bytes (always a
   * ReBirth 2.x container). Empty when the save failed — see
   * `lastSaveError()`.
   */
  saveRbs(): Uint8Array;
  /** Last `saveRbs()` error, empty on success. */
  lastSaveError(): string;
  isPlaying(): boolean;
  getProcessedBlockCount(): number;
  renderTestBlock(numFrames: number): number;
  /**
   * Start a chunked offline bounce. `deviceIndex` 0-3 renders a stem;
   * MASTER_BUS (255) renders the full mix. False when nothing is loaded or
   * the chunk buffer could not be allocated.
   */
  beginOfflineRender(deviceIndex: number): boolean;
  /**
   * Render up to `frames` (at most OFFLINE_CHUNK_FRAMES) more frames as
   * interleaved little-endian 16-bit stereo, no WAV header. Returns a
   * JS-owned Uint8Array of `rendered × 4` bytes; empty outside a begin/end
   * pair. The WASM heap only ever holds one chunk.
   */
  renderOfflineChunk(frames: number): Uint8Array;
  /** Finish a chunked bounce and release its chunk buffer. */
  endOfflineRender(): void;
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
  parse(ptr: number, size: number): WasmSongSummary | undefined;
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
