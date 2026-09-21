/**
 * Pattern-step editing model for the studio grid.
 *
 * Pure functions plus a tiny undo stack, so the click/keyboard handling in
 * `player-studio-view.ts` stays wiring and this stays testable without a DOM
 * or a WASM build.
 *
 * The engine owns the song (see `RbsAudioEngine::setStep`). Everything here
 * produces a **patch** — one `StepData` for one `(device, bank, pattern,
 * step)` slot — and an inverse patch for undo. Nothing here clones a
 * `ParsedSong`: a clone per keystroke would blow the JS heap next to the
 * fixed 64 MiB WASM one, and `ParsedSong` cannot carry TRAK automation back
 * across Embind anyway.
 *
 * Edits live in the engine's working copy only. They are not written back to
 * the `.rbs` file — reloading the file from disk restores the original.
 */

import type { DeviceId, ParsedSong, Pattern, StepData } from '../types/wasm-audio-song';
import type { WasmStepData } from '../types/wasm-audio-song';
import { isAcidDevice } from './player-studio';

/** Default note written when a 303 step is switched on (C3). */
export const DEFAULT_ACID_NOTE = 48;

/** One drum instrument, and where its bit lives in `StepData`. */
export interface DrumHitSpec {
  key: string;
  label: string;
  /** Which byte the bit belongs to — `note` (primary) or `drumExtra`. */
  field: 'note' | 'drumExtra';
  bit: number;
}

/** Must match `DrumHit` / `DrumExtra` in `src/wasm/cpp/synth/DrumBitfield.h`. */
export const DRUM_HITS: DrumHitSpec[] = [
  { key: 'bd', label: 'BD', field: 'note', bit: 0x01 },
  { key: 'sd', label: 'SD', field: 'note', bit: 0x02 },
  { key: 'lt', label: 'LT', field: 'note', bit: 0x04 },
  { key: 'mt', label: 'MT', field: 'note', bit: 0x08 },
  { key: 'ht', label: 'HT', field: 'note', bit: 0x10 },
  { key: 'ch', label: 'CH', field: 'note', bit: 0x20 },
  { key: 'oh', label: 'OH', field: 'note', bit: 0x40 },
  { key: 'cl', label: 'CL', field: 'note', bit: 0x80 },
  { key: 'cp', label: 'CP', field: 'drumExtra', bit: 0x01 },
  { key: 'ma', label: 'MA', field: 'drumExtra', bit: 0x02 },
  { key: 'rs', label: 'RS', field: 'drumExtra', bit: 0x04 },
];

export function findDrumHit(key: string): DrumHitSpec | null {
  return DRUM_HITS.find((hit) => hit.key === key) ?? null;
}

/** Normalise a UI `StepData` (optional fields) into the WASM-facing shape. */
export function toWasmStep(step: StepData | undefined | null): WasmStepData {
  return {
    active: Boolean(step?.active),
    note: step?.note ?? 0,
    drumExtra: step?.drumExtra ?? 0,
    accent: Boolean(step?.accent),
    slide: Boolean(step?.slide),
  };
}

export const EMPTY_STEP: Readonly<WasmStepData> = Object.freeze({
  active: false,
  note: 0,
  drumExtra: 0,
  accent: false,
  slide: false,
});

/**
 * TB-303 click cycle: `off → note-on → accent → slide → off`.
 *
 * Deliberately four flat states rather than a piano roll. `note` picks the
 * pitch a fresh note-on gets; an already-sounding step keeps whatever pitch
 * the file gave it.
 */
export function cycleAcidStep(current: WasmStepData, note = DEFAULT_ACID_NOTE): WasmStepData {
  const pitch = current.note || note;
  if (!current.active) {
    return { active: true, note: pitch, drumExtra: 0, accent: false, slide: false };
  }
  if (!current.accent && !current.slide) {
    return { active: true, note: pitch, drumExtra: 0, accent: true, slide: false };
  }
  if (current.accent && !current.slide) {
    return { active: true, note: pitch, drumExtra: 0, accent: false, slide: true };
  }
  return { ...EMPTY_STEP };
}

/**
 * Drum click: toggle one instrument's bit.
 *
 * A step is active exactly while it has at least one hit, so clearing the
 * last bit switches the step off rather than leaving a silent "active" step
 * the sequencer would still trigger.
 */
export function toggleDrumHit(current: WasmStepData, hit: DrumHitSpec): WasmStepData {
  const next: WasmStepData = { ...current, slide: false };
  if (hit.field === 'note') next.note = (current.note ?? 0) ^ hit.bit;
  else next.drumExtra = (current.drumExtra ?? 0) ^ hit.bit;
  const hasHit = (next.note ?? 0) !== 0 || (next.drumExtra ?? 0) !== 0;
  next.active = hasHit;
  if (!hasHit) next.accent = false;
  return next;
}

/** Shift-click: accent an already-sounding step (a no-op on a silent one). */
export function toggleAccent(current: WasmStepData): WasmStepData {
  if (!current.active) return { ...current };
  return { ...current, accent: !current.accent };
}

/** The patch a click produces, given the device family and modifier keys. */
export function nextStepValue(
  current: WasmStepData,
  deviceId: DeviceId,
  options: { accentOnly?: boolean; note?: number; drumHit?: DrumHitSpec | null } = {}
): WasmStepData {
  if (options.accentOnly) return toggleAccent(current);
  if (isAcidDevice(deviceId)) return cycleAcidStep(current, options.note ?? DEFAULT_ACID_NOTE);
  const hit = options.drumHit ?? DRUM_HITS[0];
  return toggleDrumHit(current, hit);
}

// ── Undo ────────────────────────────────────────────────────────────

/** An inverse patch: the slot, plus the `StepData` that was there before. */
export interface StepEdit {
  deviceId: DeviceId;
  bank: number;
  patternIndex: number;
  stepIndex: number;
  previous: WasmStepData;
}

export interface EditHistory {
  push(edit: StepEdit): void;
  pop(): StepEdit | null;
  get size(): number;
  clear(): void;
}

/**
 * Bounded inverse-patch stack.
 *
 * 64 entries is a few kilobytes — the whole point of storing patches rather
 * than song clones. Oldest edits fall off the bottom once it is full.
 */
export function createEditHistory(limit = 64): EditHistory {
  const stack: StepEdit[] = [];
  return {
    push(edit: StepEdit) {
      stack.push(edit);
      if (stack.length > limit) stack.shift();
    },
    pop() {
      return stack.pop() ?? null;
    },
    get size() {
      return stack.length;
    },
    clear() {
      stack.length = 0;
    },
  };
}

// ── UI-song mirror ──────────────────────────────────────────────────

function emptyUiSteps(): StepData[] {
  return Array.from({ length: 16 }, () => ({
    active: false,
    note: 0,
    drumExtra: 0,
    accent: false,
    slide: false,
  }));
}

/**
 * Mirror an accepted edit into the UI's `ParsedSong` summary.
 *
 * The engine's working copy is authoritative; this only keeps the object the
 * grid and the MIDI exporter read from in step with it, without another trip
 * across Embind.
 */
export function applyStepToSong(
  song: ParsedSong,
  deviceId: DeviceId,
  bank: number,
  patternIndex: number,
  stepIndex: number,
  step: WasmStepData
): Pattern | null {
  if (stepIndex < 0 || stepIndex > 15) return null;
  let pattern = song.patterns.find(
    (p) => p.deviceId === deviceId && p.bank === bank && p.patternIndex === patternIndex
  );
  if (!pattern) {
    pattern = { deviceId, bank, patternIndex, steps: emptyUiSteps() };
    song.patterns.push(pattern);
  }
  while (pattern.steps.length < 16) pattern.steps.push(emptyUiSteps()[0]);
  pattern.steps[stepIndex] = { ...step };
  return pattern;
}
