/**
 * UI-facing and WASM-facing **song** types for the ReBirth RB-338 engine.
 *
 * This file owns one job: the shape of a parsed `.rbs` song — devices,
 * patterns, steps and arrangement — on both sides of the Embind boundary.
 * Mod (`.rbm`) types live in `wasm-audio-mod.ts`; engine handles and the
 * Emscripten module surface live in `wasm-audio-engine.ts`.
 *
 * The single source of truth for the C++ <-> TypeScript contract is
 * `src/wasm/CONTRACT.md`. If you change a `Wasm*` type here, update that
 * document and the matching C++ struct / Embind registration, then run
 * `npm run contract:check`.
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
export type DeviceId = 'tb303-a' | 'tb303-b' | 'tr808' | 'tr909';

export interface DeviceState {
  deviceId: DeviceId;
  /** Current knob values (key = knob name, value = 0.0–1.0 normalized) */
  knobs: Record<string, number>;
  /** Whether this device is muted in the mixer */
  muted: boolean;
  level: number;
  pan: number;
  waveform: number;
  initialPatternBank: number;
  initialPatternIndex: number;
  /**
   * 808/909 per-instrument kit bytes (0–127), indexed like `DRUM_KIT_KNOBS`
   * in `player-studio.ts`. Empty for a TB-303 or a song from a summary that
   * predates the kit.
   */
  kit?: number[];
}

/** A single pattern (up to 16 steps) for one device (UI-facing) */
export interface Pattern {
  /** Which device this pattern belongs to */
  deviceId: string;
  /** Bank index 0–3 (A–D) */
  bank: number;
  /** Pattern index 0–7 within the bank */
  patternIndex: number;
  /** Plays with the song's shuffle amount (the slot's shuffle flag). */
  shuffle?: boolean;
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
  /** Packed drum bits for 808/909 (`note` + extra hits). */
  drumExtra?: number;
  /** TR-909: per-instrument accent / flam bits, in `note | drumExtra << 8` order. */
  drumAccent?: number;
  drumFlam?: number;
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

// ═══════════════════════════════════════════════════════════════════
// WASM-facing song types — must match C++ structs field-for-field
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
  /** TR-909 only: accented instruments, one bit per hit (`note | drumExtra << 8`). */
  drumAccent: number;
  /** TR-909 only: flammed instruments, same bit order. */
  drumFlam: number;
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
  delaySend: number;
  /** 808/909 kit bytes (DrumKit.h); the TB-303 carries an unused placeholder. */
  kit: number[];
}

/** Pattern as encoded in C++ `Pattern` */
export interface WasmPattern {
  deviceId: WasmDeviceId;
  bank: number;
  patternIndex: number;
  length: number;
  shuffle: boolean;
  steps: WasmStepData[];
}

/** Parsed song as encoded in C++ `ParsedSong` */
export interface WasmDelaySettings {
  enabled: boolean;
  /** Delay time in sixteenths (1–32), triplet sixteenths when `triplet`. */
  steps: number;
  triplet: boolean;
  feedback: number;
  /** Delay return pan, 0 = left, 64 = centre, 127 = right. */
  pan: number;
}

/** MIXR insert-target ids for `comp.target` / `pcf.target`. */
export const FX_TARGET = { none: 0, master: 1, firstDevice: 2 } as const;

export interface WasmPcfSettings {
  enabled: boolean;
  cutoff: number;
  resonance: number;
  envAmount: number;
  /** Trigger pattern 0–54 (decoded and saved; not yet audible). */
  pattern: number;
  decay: number;
  lowpass: boolean;
  /** Insert device: 0 none, 1 master, 2–5 the four devices. */
  target: number;
}

export interface WasmDistSettings {
  enabled: boolean;
  drive: number;
  mix: number;
}

export interface WasmCompSettings {
  enabled: boolean;
  threshold: number;
  ratio: number;
  attack: number;
  /** Insert device: 0 none, 1 master, 2–5 the four devices. */
  target: number;
}

export interface WasmSongFxSettings {
  masterLevel: number;
  delay: WasmDelaySettings;
  pcf: WasmPcfSettings;
  dist: WasmDistSettings;
  comp: WasmCompSettings;
}

/**
 * Load-time song summary, as encoded in C++ `SongSummary` (RbsTypes.h).
 *
 * Patterns and the arrangement are not in it: fetch them with
 * `RbsAudioEngineInstance.getPattern()` / `getArrangement()`. TRAK
 * automation never crosses Embind at all.
 */
export interface WasmSongSummary {
  title: string;
  author: string;
  infoText: string;
  creatorUrl: string;
  bpm: number;
  version: number;
  headVersion: number;
  globSubFormat: number;
  showInfoOnOpen: boolean;
  /** 0 = pattern mode, 1 = song mode. */
  playMode: number;
  loopEnabled: boolean;
  /** Loop locators as whole 0-based bars. */
  loopStartBar: number;
  loopEndBar: number;
  /** Song shuffle amount 0–127, for patterns whose `shuffle` flag is set. */
  shuffle: number;
  /** ReBirth 1.x "vintage" sound switch (decoded, not yet audible). */
  vintage: boolean;
  devices: WasmDeviceState[];
  fx: WasmSongFxSettings;
  /** Bars in the arrangement (0 when the song has none). */
  barCount: number;
  /** Per device: bit `bank * 8 + index` set when the song stores that slot. */
  patternSlots: number[];
}
