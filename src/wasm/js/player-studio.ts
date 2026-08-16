/**
 * Pattern-grid + device-panel helpers for the in-browser player.
 * Pure functions so Playwright/compile-time tests can assert mapping without WASM.
 */

import type { DeviceId, ParsedSong, Pattern, StepData } from '../types/wasm-audio';

export const STUDIO_DEVICES: { id: DeviceId; label: string }[] = [
  { id: 'tb303-a', label: '303 A' },
  { id: 'tb303-b', label: '303 B' },
  { id: 'tr808', label: '808' },
  { id: 'tr909', label: '909' },
];

export const DEVICE_INDEX: Record<DeviceId, number> = {
  'tb303-a': 0,
  'tb303-b': 1,
  tr808: 2,
  tr909: 3,
};

/** Must match C++ DeviceParamId. */
export const DeviceParam = {
  Tune: 0,
  Cutoff: 1,
  Resonance: 2,
  EnvMod: 3,
  Decay: 4,
  Accent: 5,
  Waveform: 6,
  Level: 7,
  Pan: 8,
  Mute: 9,
} as const;

export function isAcidDevice(id: DeviceId): boolean {
  return id === 'tb303-a' || id === 'tb303-b';
}

export function pickPattern(
  song: ParsedSong,
  deviceId: DeviceId,
  bank: number,
  patternIndex: number
): Pattern | null {
  const exact = song.patterns.find(
    (p) => p.deviceId === deviceId && p.bank === bank && p.patternIndex === patternIndex
  );
  if (exact) return exact;
  return song.patterns.find((p) => p.deviceId === deviceId) ?? null;
}

export function defaultPatternCoords(
  song: ParsedSong,
  deviceId: DeviceId
): { bank: number; patternIndex: number } {
  const device = song.devices.find((d) => d.deviceId === deviceId);
  if (device) {
    return {
      bank: device.initialPatternBank ?? 0,
      patternIndex: device.initialPatternIndex ?? 0,
    };
  }
  const first = song.patterns.find((p) => p.deviceId === deviceId);
  return { bank: first?.bank ?? 0, patternIndex: first?.patternIndex ?? 0 };
}

const DRUM_PRIMARY: Array<{ bit: number; label: string }> = [
  { bit: 0x01, label: 'BD' },
  { bit: 0x02, label: 'SD' },
  { bit: 0x04, label: 'LT' },
  { bit: 0x08, label: 'MT' },
  { bit: 0x10, label: 'HT' },
  { bit: 0x20, label: 'CH' },
  { bit: 0x40, label: 'OH' },
  { bit: 0x80, label: 'CL' },
];

const DRUM_EXTRA: Array<{ bit: number; label: string }> = [
  { bit: 0x01, label: 'CP' },
  { bit: 0x02, label: 'MA' },
  { bit: 0x04, label: 'RS' },
];

export function drumHitLabels(step: StepData): string[] {
  const note = step.note ?? 0;
  const extra = step.drumExtra ?? 0;
  const labels: string[] = [];
  for (const hit of DRUM_PRIMARY) {
    if (note & hit.bit) labels.push(hit.label);
  }
  for (const hit of DRUM_EXTRA) {
    if (extra & hit.bit) labels.push(hit.label);
  }
  return labels;
}

export function midiNoteName(note: number | undefined): string {
  if (note == null || !Number.isFinite(note)) return '';
  const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const n = Math.round(note);
  return `${names[((n % 12) + 12) % 12]}${Math.floor(n / 12) - 1}`;
}

export interface StudioStepCell {
  index: number;
  active: boolean;
  accent: boolean;
  slide: boolean;
  label: string;
}

export function buildStepCells(pattern: Pattern | null, deviceId: DeviceId): StudioStepCell[] {
  const cells: StudioStepCell[] = [];
  const acid = isAcidDevice(deviceId);
  for (let i = 0; i < 16; i++) {
    const step = pattern?.steps[i];
    const active = Boolean(step?.active);
    let label = '';
    if (active && step) {
      label = acid ? midiNoteName(step.note) : drumHitLabels(step).join(' ');
    }
    cells.push({
      index: i,
      active,
      accent: Boolean(step?.accent),
      slide: Boolean(step?.slide),
      label,
    });
  }
  return cells;
}
