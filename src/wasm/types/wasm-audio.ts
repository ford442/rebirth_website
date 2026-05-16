/**
 * Shared TypeScript interfaces for the ReBirth RB-338 WASM audio engine.
 *
 * These types describe the data that flows from the WASM parser back to
 * JavaScript, and the control messages that JS sends to the WASM player.
 */

/** Top-level status of the in-browser player UI */
export type PlayerStatus = 'idle' | 'loading' | 'ready' | 'playing' | 'error';

/** Result of parsing an .rbs file in WASM */
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

/** State snapshot for one of the four ReBirth devices */
export interface DeviceState {
  deviceId: 'tb303-a' | 'tb303-b' | 'tr808' | 'tr909';
  /** Current knob values (key = knob name, value = 0.0–1.0 normalized) */
  knobs: Record<string, number>;
  /** Whether this device is muted in the mixer */
  muted: boolean;
}

/** A single pattern (up to 16 steps) for one device */
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

/** Data for one step in a pattern */
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

/** One bar in the song arrangement */
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

/** Feature flags that can disable sub-devices at runtime */
export interface EngineFeatures {
  tb303_a: boolean;
  tb303_b: boolean;
  tr808: boolean;
  tr909: boolean;
  distortion: boolean;
  compressor: boolean;
  delay: boolean;
}

/** Runtime engine configuration passed from JS to WASM on init */
export interface EngineConfig {
  /** Host AudioContext sample rate (Hz) */
  sampleRate: number;
  /** Render quantum size in frames (typically 128) */
  bufferSize: number;
  /** Per-module feature flags */
  features: EngineFeatures;
}

/** Error info returned when parsing or playback fails */
export interface EngineError {
  code: string;
  message: string;
}
