/**
 * WASM -> UI mapping for the types in `wasm-audio.ts`.
 *
 * Pure data transformation (no AudioContext, no Embind calls beyond reading
 * the vector handles the parser handed back) — kept separate from
 * `WasmAudioBridge.ts` so that file stays I/O + engine calls, and so this
 * mapping can be unit-tested / read without pulling in Web Audio APIs.
 */

import type { EmbindVector } from './wasm-audio-engine';
import type {
  ModResourceInfo,
  ParsedMod,
  WasmModLoadReport,
  WasmModSampleReportEntry,
} from './wasm-audio-mod';
import type {
  ArrangementStep,
  DeviceState,
  ParsedSong,
  Pattern,
  PatternRef,
  WasmArrangementBar,
  WasmDeviceId,
  WasmDeviceState,
  WasmSongSummary,
  WasmPattern,
} from './wasm-audio-song';
import {
  MOD_LOAD_STATUSES,
  MOD_RESOURCE_KINDS,
  MOD_SAMPLE_SLOTS,
  SAMPLE_DECODE_STATUSES,
} from './wasm-audio-mod';

/** Numeric `WasmDeviceId` -> UI-facing device id string. Must match `DeviceId` in C++ `main.cpp`. */
export const WASM_DEVICE_ID_TO_UI: Record<WasmDeviceId, DeviceState['deviceId']> = {
  0: 'tb303-a',
  1: 'tb303-b',
  2: 'tr808',
  3: 'tr909',
};

/** Reads every element out of an Embind vector handle, then deletes it. */
function consumeVector<T>(vector: EmbindVector<T>): T[] {
  const values: T[] = [];
  try {
    for (let index = 0; index < vector.size(); index += 1) {
      values.push(vector.get(index));
    }
    return values;
  } finally {
    vector.delete();
  }
}

function toUiDeviceState(wasmDevice: WasmDeviceState): DeviceState {
  return {
    deviceId: WASM_DEVICE_ID_TO_UI[wasmDevice.id],
    knobs: {
      tune: wasmDevice.tune,
      cutoff: wasmDevice.cutoff,
      resonance: wasmDevice.resonance,
      envMod: wasmDevice.envMod,
      decay: wasmDevice.decay,
      accent: wasmDevice.accent,
    },
    muted: wasmDevice.muted,
    level: wasmDevice.level,
    pan: wasmDevice.pan,
    waveform: wasmDevice.waveform,
    initialPatternBank: wasmDevice.initialPatternBank,
    initialPatternIndex: wasmDevice.initialPatternIndex,
    kit: wasmDevice.id >= 2 && wasmDevice.kit ? Array.from(wasmDevice.kit) : [],
  };
}

function toUiPattern(wasmPattern: WasmPattern): Pattern {
  return {
    deviceId: WASM_DEVICE_ID_TO_UI[wasmPattern.deviceId],
    bank: wasmPattern.bank,
    patternIndex: wasmPattern.patternIndex,
    shuffle: Boolean(wasmPattern.shuffle),
    steps: wasmPattern.steps.map((s) => ({
      active: s.active,
      note: s.note === 0 ? undefined : s.note,
      accent: s.accent,
      slide: s.slide,
      drumExtra: s.drumExtra,
      drumAccent: s.drumAccent ?? 0,
      drumFlam: s.drumFlam ?? 0,
    })),
  };
}

function toUiArrangementStep(wasmBar: WasmArrangementBar): ArrangementStep {
  const patternRefs: Record<string, PatternRef> = {};
  wasmBar.devicePatterns.forEach((ref, index) => {
    const label = WASM_DEVICE_ID_TO_UI[index as WasmDeviceId];
    if (label) {
      patternRefs[label] = { bank: ref.bank, index: ref.index };
    }
  });
  return {
    bar: wasmBar.barNumber,
    patternRefs,
  };
}

/** Where the patterns and arrangement behind a `WasmSongSummary` come from. */
export interface SongPatternSource {
  getPattern(deviceId: number, bank: number, patternIndex: number): WasmPattern | undefined;
  getArrangement(): EmbindVector<WasmArrangementBar>;
}

/**
 * Builds the UI-facing `ParsedSong` from the slim Embind `WasmSongSummary`
 * (returned by `loadSongFromBytes`), fetching each pattern the summary says
 * the song stores, and the arrangement, from `source` — the engine's working
 * copy. Consumes (and deletes) the arrangement vector handle it fetches.
 */
export function toUiParsedSong(summary: WasmSongSummary, source: SongPatternSource): ParsedSong {
  const patterns: Pattern[] = [];
  summary.patternSlots.forEach((mask, deviceId) => {
    for (let slot = 0; slot < 32; slot += 1) {
      if (((mask >>> slot) & 1) === 0) continue;
      const pattern = source.getPattern(deviceId, slot >> 3, slot & 7);
      if (pattern) patterns.push(toUiPattern(pattern));
    }
  });
  const arrangement = consumeVector(source.getArrangement());
  return {
    title: summary.title,
    author: summary.author,
    bpm: summary.bpm,
    devices: summary.devices.map(toUiDeviceState),
    patterns,
    arrangement: arrangement.map(toUiArrangementStep),
  };
}

function toUiModResource(entry: WasmModSampleReportEntry): ModResourceInfo {
  return {
    name: entry.name,
    kind: MOD_RESOURCE_KINDS[entry.kind] ?? 'other',
    slot: MOD_SAMPLE_SLOTS[entry.slot] ?? 'unknown',
    byteSize: entry.byteSize,
    frameCount: entry.frameCount,
    sampleRate: entry.sampleRate,
    channels: entry.channels,
    bitDepth: entry.bitDepth,
    decodeStatus: SAMPLE_DECODE_STATUSES[entry.decodeStatus] ?? 'unknown-format',
    loaded: entry.loaded,
  };
}

/**
 * Converts the raw Embind `WasmModLoadReport` into the UI-facing `ParsedMod`
 * shape. Consumes (and deletes) the `resources` Embind vector handle as a
 * side effect — call this exactly once per report.
 */
export function toUiParsedMod(report: WasmModLoadReport): ParsedMod {
  const resources = consumeVector(report.resources);
  return {
    title: report.title,
    description: report.description,
    copyright: report.copyright,
    resources: resources.map(toUiModResource),
    status: MOD_LOAD_STATUSES[report.status] ?? 'no-samples',
    loadedSlots: report.loadedSlots,
    skinCount: report.skinCount,
    usedFrames: report.usedFrames,
    capacityFrames: report.capacityFrames,
  };
}
