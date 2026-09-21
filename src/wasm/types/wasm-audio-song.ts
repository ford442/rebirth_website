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

import type { EmbindVector } from './wasm-audio-engine';

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
  /** Packed drum bits for 808/909 (`note` + extra hits). */
  drumExtra?: number;
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

/** Parsed song as encoded in C++ `ParsedSong` */
export interface WasmDelaySettings {
  enabled: boolean;
  time: number;
  feedback: number;
  wet: number;
}

export interface WasmPcfSettings {
  enabled: boolean;
  cutoff: number;
  resonance: number;
  envAmount: number;
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
}

export interface WasmSongFxSettings {
  masterLevel: number;
  delay: WasmDelaySettings;
  pcf: WasmPcfSettings;
  dist: WasmDistSettings;
  comp: WasmCompSettings;
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
  fx: WasmSongFxSettings;
}
