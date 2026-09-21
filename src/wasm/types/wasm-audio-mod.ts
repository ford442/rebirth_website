/**
 * `.rbm` mod types — enum label tables, the Embind load report, and the
 * UI-facing summary the bridge builds from it.
 *
 * Every `MOD_*` / `SAMPLE_*` array below mirrors a C++ enum *in declaration
 * order*, so the index is the enum value. Keep them in sync with
 * `cpp/parser/RbmTypes.h` and `npm run contract:check`.
 */

import type { EmbindVector } from './wasm-audio-engine';

/** Mirrors C++ `ModResourceKind`. */
export const MOD_RESOURCE_KINDS = ['sample', 'skin', 'song', 'other'] as const;
export type ModResourceKindLabel = (typeof MOD_RESOURCE_KINDS)[number];

/** Mirrors C++ `ModLoadStatus`, in declaration order. */
export const MOD_LOAD_STATUSES = [
  'ok',
  'not-initialised',
  'no-samples',
  'arena-exhausted',
  'partial',
] as const;
export type ModLoadStatusLabel = (typeof MOD_LOAD_STATUSES)[number];

/** Mirrors C++ `SampleDecodeStatus`, in declaration order. */
export const SAMPLE_DECODE_STATUSES = [
  'ok',
  'unknown-format',
  'malformed',
  'unsupported-encoding',
  'empty',
  'destination-too-small',
] as const;
export type SampleDecodeStatusLabel = (typeof SAMPLE_DECODE_STATUSES)[number];

/**
 * Mirrors C++ `ModSampleSlot`, in declaration order — index === enum value.
 * Keep in sync with `modSampleSlotName()` in cpp/parser/RbmTypes.h.
 */
export const MOD_SAMPLE_SLOTS = [
  'unknown',
  'tr808-kick',
  'tr808-snare',
  'tr808-low-tom',
  'tr808-mid-tom',
  'tr808-high-tom',
  'tr808-closed-hat',
  'tr808-open-hat',
  'tr808-rimshot',
  'tr808-clap',
  'tr808-clave',
  'tr808-cymbal',
  'tr808-maracas',
  'tr909-kick',
  'tr909-snare',
  'tr909-low-tom',
  'tr909-mid-tom',
  'tr909-high-tom',
  'tr909-closed-hat',
  'tr909-open-hat',
  'tr909-rimshot',
  'tr909-clap',
  'tr909-crash',
  'tr909-ride',
  'tb303-saw',
  'tb303-square',
] as const;
export type ModSampleSlotLabel = (typeof MOD_SAMPLE_SLOTS)[number];

/**
 * One resource inside a `.rbm`, as reported by C++ `ModSampleReportEntry`.
 *
 * Note there is deliberately no `bytes` field: sample and skin payloads never
 * cross into JS. They stay in WASM memory, where the engine reads them
 * directly. `byteSize` is the source payload's size for display only.
 */
export interface WasmModSampleReportEntry {
  name: string;
  kind: number;
  slot: number;
  byteSize: number;
  frameCount: number;
  sampleRate: number;
  channels: number;
  bitDepth: number;
  decodeStatus: number;
  loaded: boolean;
}

/** Mirrors C++ `ModLoadReport`. */
export interface WasmModLoadReport {
  title: string;
  description: string;
  copyright: string;
  resources: EmbindVector<WasmModSampleReportEntry>;
  status: number;
  loadedSlots: number;
  skinCount: number;
  usedFrames: number;
  capacityFrames: number;
}

/** UI-facing resource entry, with enums resolved to labels. */
export interface ModResourceInfo {
  name: string;
  kind: ModResourceKindLabel;
  slot: ModSampleSlotLabel;
  byteSize: number;
  frameCount: number;
  sampleRate: number;
  channels: number;
  bitDepth: number;
  decodeStatus: SampleDecodeStatusLabel;
  loaded: boolean;
}

/** UI-facing mod summary, produced by the bridge from `WasmModLoadReport`. */
export interface ParsedMod {
  title: string;
  description: string;
  copyright: string;
  resources: ModResourceInfo[];
  status: ModLoadStatusLabel;
  loadedSlots: number;
  skinCount: number;
  usedFrames: number;
  capacityFrames: number;
}
