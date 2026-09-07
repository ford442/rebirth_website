/**
 * WASM -> UI mapping for the types in `wasm-audio.ts`.
 *
 * Pure data transformation (no AudioContext, no Embind calls beyond reading
 * the vector handles the parser handed back) — kept separate from
 * `WasmAudioBridge.ts` so that file stays I/O + engine calls, and so this
 * mapping can be unit-tested / read without pulling in Web Audio APIs.
 */

import type {
  ArrangementStep,
  DeviceState,
  EmbindVector,
  ModResourceInfo,
  Pattern,
  ParsedMod,
  ParsedSong,
  PatternRef,
  WasmArrangementBar,
  WasmDeviceId,
  WasmDeviceState,
  WasmModLoadReport,
  WasmModSampleReportEntry,
  WasmParsedSong,
  WasmPattern,
} from './wasm-audio';
import {
  MOD_LOAD_STATUSES,
  MOD_RESOURCE_KINDS,
  MOD_SAMPLE_SLOTS,
  SAMPLE_DECODE_STATUSES,
} from './wasm-audio';

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
  };
}

function toUiPattern(wasmPattern: WasmPattern): Pattern {
  return {
    deviceId: WASM_DEVICE_ID_TO_UI[wasmPattern.deviceId],
    bank: wasmPattern.bank,
    patternIndex: wasmPattern.patternIndex,
    steps: wasmPattern.steps.map((s) => ({
      active: s.active,
      note: s.note === 0 ? undefined : s.note,
      accent: s.accent,
      slide: s.slide,
      drumExtra: s.drumExtra,
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

/**
 * Converts the raw Embind `WasmParsedSong` (returned by `RbsParser::parse`)
 * into the UI-facing `ParsedSong` shape. Consumes (and deletes) the
 * `patterns` / `arrangement` Embind vector handles as a side effect — call
 * this exactly once per parsed song.
 */
export function toUiParsedSong(wasmSong: WasmParsedSong): ParsedSong {
  const patterns = consumeVector(wasmSong.patterns);
  const arrangement = consumeVector(wasmSong.arrangement);
  return {
    title: wasmSong.title,
    author: wasmSong.author,
    bpm: wasmSong.bpm,
    devices: wasmSong.devices.map(toUiDeviceState),
    patterns: patterns.map(toUiPattern),
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
