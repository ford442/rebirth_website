/**
 * Pattern-grid + device-panel helpers for the in-browser player.
 * Pure functions so Playwright/compile-time tests can assert mapping without WASM.
 */

import type { DeviceId, ParsedSong, Pattern, StepData } from '../types/wasm-audio-song';

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

/** Short display label per device, e.g. for metadata chips and studio tabs. */
export const STUDIO_DEVICE_LABEL: Record<DeviceId, string> = Object.fromEntries(
  STUDIO_DEVICES.map((d) => [d.id, d.label])
) as Record<DeviceId, string>;

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
  KitBase: 10,
} as const;

/** Drum-kit bytes per device (C++ `DRUM_KIT_BYTES`); `KitBase + i` is byte `i`. */
export const DRUM_KIT_BYTES = 28;

/** Highest `DeviceParam` id — what the bridge clamps an incoming id to. */
export const MAX_DEVICE_PARAM_ID = DeviceParam.KitBase + DRUM_KIT_BYTES - 1;

/** One studio knob for a drum-kit byte. A `switch` is a two-way selector. */
export interface DrumKitKnob {
  /** Index into `DeviceState.kit` (TRAK controller − 3). */
  index: number;
  label: string;
  /** Selector switch: [label at 0, label when set]. */
  switch?: [string, string];
}

/**
 * The 808 / 909 front panels, in panel order — mirrors `Kit808` / `Kit909`
 * in `src/wasm/cpp/parser/DrumKit.h` (index = controller id − 3).
 */
export const DRUM_KIT_KNOBS: Record<'tr808' | 'tr909', DrumKitKnob[]> = {
  tr808: [
    { index: 0, label: 'BD LVL' },
    { index: 1, label: 'BD TONE' },
    { index: 2, label: 'BD DEC' },
    { index: 3, label: 'SD LVL' },
    { index: 4, label: 'SD TONE' },
    { index: 5, label: 'SD SNAP' },
    { index: 6, label: 'LT LVL' },
    { index: 7, label: 'LT TUNE' },
    { index: 8, label: 'LT/LC', switch: ['TOM', 'CONGA'] },
    { index: 9, label: 'MT LVL' },
    { index: 10, label: 'MT TUNE' },
    { index: 11, label: 'MT/MC', switch: ['TOM', 'CONGA'] },
    { index: 12, label: 'HT LVL' },
    { index: 13, label: 'HT TUNE' },
    { index: 14, label: 'HT/HC', switch: ['TOM', 'CONGA'] },
    { index: 15, label: 'RS LVL' },
    { index: 16, label: 'RS/CL', switch: ['CLAVES', 'RIM'] },
    { index: 17, label: 'CP LVL' },
    { index: 18, label: 'CP/MA', switch: ['MARACAS', 'CLAP'] },
    { index: 19, label: 'CB LVL' },
    { index: 20, label: 'CY LVL' },
    { index: 21, label: 'CY TONE' },
    { index: 22, label: 'CY DEC' },
    { index: 23, label: 'OH LVL' },
    { index: 24, label: 'OH DEC' },
    { index: 25, label: 'CH LVL' },
  ],
  tr909: [
    { index: 0, label: 'BD LVL' },
    { index: 1, label: 'BD TUNE' },
    { index: 2, label: 'BD ATK' },
    { index: 3, label: 'BD DEC' },
    { index: 4, label: 'SD LVL' },
    { index: 5, label: 'SD TUNE' },
    { index: 6, label: 'SD TONE' },
    { index: 7, label: 'SD SNAP' },
    { index: 8, label: 'LT LVL' },
    { index: 9, label: 'LT TUNE' },
    { index: 10, label: 'LT DEC' },
    { index: 11, label: 'MT LVL' },
    { index: 12, label: 'MT TUNE' },
    { index: 13, label: 'MT DEC' },
    { index: 14, label: 'HT LVL' },
    { index: 15, label: 'HT TUNE' },
    { index: 16, label: 'HT DEC' },
    { index: 17, label: 'RS LVL' },
    { index: 18, label: 'CP LVL' },
    { index: 19, label: 'HH LVL' },
    { index: 20, label: 'CH DEC' },
    { index: 21, label: 'OH DEC' },
    { index: 22, label: 'CR LVL' },
    { index: 23, label: 'CR TUNE' },
    { index: 24, label: 'RD LVL' },
    { index: 25, label: 'RD TUNE' },
    { index: 26, label: 'FLAM' },
  ],
};

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

/**
 * Strict slot lookup — no "first pattern for this device" fallback.
 *
 * The forgiving {@link pickPattern} is right for a read-only preview, but an
 * editable grid must show the slot the click will write to: otherwise the
 * user edits bank B pattern 5 while looking at bank A pattern 1.
 */
export function pickPatternExact(
  song: ParsedSong,
  deviceId: DeviceId,
  bank: number,
  patternIndex: number
): Pattern | null {
  return (
    song.patterns.find(
      (p) => p.deviceId === deviceId && p.bank === bank && p.patternIndex === patternIndex
    ) ?? null
  );
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

/** Labels in `note | drumExtra << 8` bit order; [808, 909] where they differ. */
const DRUM_LABELS: Array<string | [string, string]> = [
  'BD',
  'SD',
  'LT',
  'MT',
  'HT',
  'CH',
  'OH',
  ['CB', 'CR'],
  'CP',
  ['CY', 'RD'],
  'RS',
];

/**
 * The instruments a drum step hits, by the device's own names. A 909 hit
 * accented on its own is marked `!`, a flam `~`.
 */
export function drumHitLabels(step: StepData, deviceId: DeviceId = 'tr808'): string[] {
  const word = ((step.note ?? 0) & 0xff) | (((step.drumExtra ?? 0) & 0x07) << 8);
  const machine = deviceId === 'tr909' ? 1 : 0;
  const labels: string[] = [];
  DRUM_LABELS.forEach((name, bit) => {
    const mask = 1 << bit;
    if (!(word & mask)) return;
    const label = typeof name === 'string' ? name : name[machine];
    const flam = ((step.drumFlam ?? 0) & mask) !== 0;
    const strong = ((step.drumAccent ?? 0) & mask) !== 0;
    labels.push(`${flam ? '~' : ''}${label}${strong ? '!' : ''}`);
  });
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
      label = acid ? midiNoteName(step.note) : drumHitLabels(step, deviceId).join(' ');
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
