/**
 * midi-smf.ts — a Standard MIDI File writer for ReBirth patterns.
 *
 * Deliberately dependency-free. ReBirth steps are already a tiny, regular
 * structure (note, accent, slide, drum bits), so a full MIDI library would be
 * mostly dead weight; this is the ~200 lines that actually apply. If a piano
 * roll ever lands, revisit — that is the point where @tonejs/midi earns its
 * size.
 *
 * Output is SMF Type 1: one tempo track plus one track per ReBirth device,
 * which is what a DAW expects when you drag a multi-instrument file in.
 */

import type { ParsedSong, StepData, DeviceId } from '../wasm/types/wasm-audio';

/** MIDI ticks per quarter note. 96 divides evenly by 6 for triplet-free 16ths. */
export const TICKS_PER_QUARTER = 96;

/** A ReBirth step is a 16th note. */
export const TICKS_PER_STEP = TICKS_PER_QUARTER / 4;

/**
 * How a 303 slide becomes MIDI.
 *
 * ReBirth's slide is a pitch glide between consecutive notes with no
 * re-trigger. MIDI has no single portable way to say that, so we pick one and
 * document it rather than inventing a house convention:
 *
 * - `overlap` (default): the sliding note is held past the next note's start,
 *   which is how monophonic synths and most DAWs detect legato. Round-trips
 *   through every DAW, needs no CC support, and sounds right on a mono synth
 *   patch. This is what hardware-accurate 303 MIDI exports conventionally do.
 * - `cc65`: emit Portamento On/Off (CC 65) around slid notes. More literal,
 *   but only helps if the receiving instrument implements portamento.
 */
export type SlideMode = 'overlap' | 'cc65';

export interface MidiExportOptions {
  /** Defaults to the song's own tempo. */
  bpm?: number;
  slideMode?: SlideMode;
  /** MIDI channel (0-based) for the drum tracks. GM uses channel 10 (index 9). */
  drumChannel?: number;
  /** Repeat the pattern this many times. */
  bars?: number;
}

/**
 * TR-808/909 voice → General MIDI percussion key.
 *
 * GM has no 808/909 distinction, so both machines map onto the same GM keys;
 * they stay on separate tracks so a DAW can route them to different kits.
 */
export const GM_DRUM_MAP = {
  kick: 36, // Bass Drum 1
  snare: 38, // Acoustic Snare
  lowTom: 45, // Low Tom
  midTom: 47, // Low-Mid Tom
  highTom: 50, // High Tom
  closedHat: 42, // Closed Hi-Hat
  openHat: 46, // Open Hi-Hat
  clave: 75, // Claves
  clap: 39, // Hand Clap
  maracas: 70, // Maracas
  rimshot: 37, // Side Stick
} as const;

/** Drum bits packed into `StepData.note` — mirrors cpp/synth/DrumBitfield.h. */
const DRUM_HIT = {
  BD: 0x01,
  SD: 0x02,
  LT: 0x04,
  MT: 0x08,
  HT: 0x10,
  CH: 0x20,
  OH: 0x40,
  CL: 0x80,
} as const;

/** Secondary hits packed into `StepData.drumExtra`. */
const DRUM_EXTRA = {
  CP: 0x01,
  MA: 0x02,
  RS: 0x04,
} as const;

const DRUM_BIT_TO_KEY: Array<[number, number]> = [
  [DRUM_HIT.BD, GM_DRUM_MAP.kick],
  [DRUM_HIT.SD, GM_DRUM_MAP.snare],
  [DRUM_HIT.LT, GM_DRUM_MAP.lowTom],
  [DRUM_HIT.MT, GM_DRUM_MAP.midTom],
  [DRUM_HIT.HT, GM_DRUM_MAP.highTom],
  [DRUM_HIT.CH, GM_DRUM_MAP.closedHat],
  [DRUM_HIT.OH, GM_DRUM_MAP.openHat],
  [DRUM_HIT.CL, GM_DRUM_MAP.clave],
];

const DRUM_EXTRA_TO_KEY: Array<[number, number]> = [
  [DRUM_EXTRA.CP, GM_DRUM_MAP.clap],
  [DRUM_EXTRA.MA, GM_DRUM_MAP.maracas],
  [DRUM_EXTRA.RS, GM_DRUM_MAP.rimshot],
];

const ACCENT_VELOCITY = 112;
const NORMAL_VELOCITY = 80;

// ── Byte plumbing ────────────────────────────────────────────────

/** MIDI variable-length quantity: 7 bits per byte, high bit = "more follows". */
export function writeVarLength(value: number): number[] {
  const clamped = Math.max(0, Math.floor(value));
  const bytes = [clamped & 0x7f];
  let rest = clamped >> 7;
  while (rest > 0) {
    bytes.unshift((rest & 0x7f) | 0x80);
    rest >>= 7;
  }
  return bytes;
}

function be32(value: number): number[] {
  return [(value >>> 24) & 0xff, (value >>> 16) & 0xff, (value >>> 8) & 0xff, value & 0xff];
}

function be16(value: number): number[] {
  return [(value >>> 8) & 0xff, value & 0xff];
}

function ascii(text: string): number[] {
  const out: number[] = [];
  for (let i = 0; i < text.length; i += 1) {
    out.push(text.charCodeAt(i) & 0x7f);
  }
  return out;
}

function chunk(id: string, body: number[]): number[] {
  return [...ascii(id), ...be32(body.length), ...body];
}

/** One MIDI event, positioned absolutely so tracks can be built out of order. */
interface AbsoluteEvent {
  tick: number;
  /** Note-offs sort before note-ons at the same tick, so re-hits retrigger. */
  order: number;
  bytes: number[];
}

function noteOn(channel: number, key: number, velocity: number): number[] {
  return [0x90 | (channel & 0x0f), key & 0x7f, velocity & 0x7f];
}

function noteOff(channel: number, key: number): number[] {
  return [0x80 | (channel & 0x0f), key & 0x7f, 0x40];
}

function controlChange(channel: number, controller: number, value: number): number[] {
  return [0xb0 | (channel & 0x0f), controller & 0x7f, value & 0x7f];
}

/** Serialise absolute-time events into a delta-time MTrk chunk. */
function buildTrack(name: string, events: AbsoluteEvent[], extraHead: number[] = []): number[] {
  const sorted = [...events].sort((a, b) => a.tick - b.tick || a.order - b.order);

  const body: number[] = [];
  // Track name meta event at tick 0.
  body.push(...writeVarLength(0), 0xff, 0x03, ...writeVarLength(name.length), ...ascii(name));
  body.push(...extraHead);

  let lastTick = 0;
  for (const event of sorted) {
    body.push(...writeVarLength(event.tick - lastTick));
    body.push(...event.bytes);
    lastTick = event.tick;
  }

  // End of track.
  body.push(...writeVarLength(0), 0xff, 0x2f, 0x00);
  return chunk('MTrk', body);
}

// ── Pattern → events ─────────────────────────────────────────────

function stepsForDevice(song: ParsedSong, deviceId: DeviceId): StepData[] | null {
  const device = song.devices.find((d) => d.deviceId === deviceId);
  const bank = device?.initialPatternBank ?? 0;
  const index = device?.initialPatternIndex ?? 0;

  const exact = song.patterns.find(
    (p) => p.deviceId === deviceId && p.bank === bank && p.patternIndex === index
  );
  const pattern = exact ?? song.patterns.find((p) => p.deviceId === deviceId);
  if (!pattern) return null;
  return pattern.steps;
}

/**
 * Build note events for one 303 track.
 *
 * A slid step is held until the following step's note-on (and slightly past
 * it, so the overlap is unambiguous) rather than being cut at its own
 * boundary — see SlideMode.
 */
function buildAcidEvents(
  steps: StepData[],
  channel: number,
  repeats: number,
  slideMode: SlideMode
): AbsoluteEvent[] {
  const events: AbsoluteEvent[] = [];
  const stepCount = steps.length;

  for (let repeat = 0; repeat < repeats; repeat += 1) {
    const barTick = repeat * stepCount * TICKS_PER_STEP;

    for (let i = 0; i < stepCount; i += 1) {
      const step = steps[i];
      if (!step?.active) continue;

      const start = barTick + i * TICKS_PER_STEP;
      const key = Math.max(0, Math.min(127, step.note ?? 0));
      const velocity = step.accent ? ACCENT_VELOCITY : NORMAL_VELOCITY;

      // A slide reaches into the next step; overlap by a tick so DAWs read it
      // as legato rather than as two touching notes.
      let end = start + TICKS_PER_STEP;
      if (step.slide) {
        const next = steps[(i + 1) % stepCount];
        if (next?.active) end = start + TICKS_PER_STEP + 1;
      }

      if (slideMode === 'cc65' && step.slide) {
        events.push({ tick: start, order: 1, bytes: controlChange(channel, 65, 127) });
      }

      events.push({ tick: start, order: 2, bytes: noteOn(channel, key, velocity) });
      events.push({ tick: end, order: 0, bytes: noteOff(channel, key) });

      if (slideMode === 'cc65' && step.slide) {
        events.push({ tick: end, order: 1, bytes: controlChange(channel, 65, 0) });
      }
    }
  }

  return events;
}

/** Build percussion events for one drum machine track. */
function buildDrumEvents(steps: StepData[], channel: number, repeats: number): AbsoluteEvent[] {
  const events: AbsoluteEvent[] = [];
  const stepCount = steps.length;

  for (let repeat = 0; repeat < repeats; repeat += 1) {
    const barTick = repeat * stepCount * TICKS_PER_STEP;

    for (let i = 0; i < stepCount; i += 1) {
      const step = steps[i];
      if (!step?.active) continue;

      const start = barTick + i * TICKS_PER_STEP;
      const velocity = step.accent ? ACCENT_VELOCITY : NORMAL_VELOCITY;
      // Percussion is one-shot; a short fixed gate is the usual convention.
      const end = start + Math.max(1, Math.floor(TICKS_PER_STEP / 2));

      const emit = (key: number) => {
        events.push({ tick: start, order: 2, bytes: noteOn(channel, key, velocity) });
        events.push({ tick: end, order: 0, bytes: noteOff(channel, key) });
      };

      const hits = step.note ?? 0;
      for (const [bit, key] of DRUM_BIT_TO_KEY) {
        if (hits & bit) emit(key);
      }
      for (const [bit, key] of DRUM_EXTRA_TO_KEY) {
        if ((step.drumExtra ?? 0) & bit) emit(key);
      }
    }
  }

  return events;
}

// ── Public API ───────────────────────────────────────────────────

const DEVICE_TRACK_NAMES: Record<DeviceId, string> = {
  'tb303-a': 'TB-303 A',
  'tb303-b': 'TB-303 B',
  tr808: 'TR-808',
  tr909: 'TR-909',
};

/**
 * Export a song's current patterns as a Standard MIDI File (Type 1).
 *
 * This exports the *pattern* each device is sitting on, repeated `bars`
 * times — not the full arrangement. That is the useful first slice for
 * getting a loop into a DAW, and it is honest about not being ReMaker parity.
 */
export function songToMidi(song: ParsedSong, options: MidiExportOptions = {}): Uint8Array {
  const bpm = options.bpm ?? song.bpm ?? 125;
  const slideMode = options.slideMode ?? 'overlap';
  const drumChannel = options.drumChannel ?? 9; // GM percussion
  const repeats = Math.max(1, Math.floor(options.bars ?? 1));

  const tracks: number[][] = [];

  // Track 0: tempo map. microseconds per quarter note.
  const microsPerQuarter = Math.max(1, Math.round(60_000_000 / bpm));
  const tempoHead = [
    ...writeVarLength(0),
    0xff,
    0x51,
    0x03,
    (microsPerQuarter >> 16) & 0xff,
    (microsPerQuarter >> 8) & 0xff,
    microsPerQuarter & 0xff,
    // 4/4 at 24 MIDI clocks per metronome click, 8 32nds per quarter.
    ...writeVarLength(0),
    0xff,
    0x58,
    0x04,
    4,
    2,
    24,
    8,
  ];
  tracks.push(buildTrack(song.title || 'ReBirth RB-338', [], tempoHead));

  const deviceOrder: DeviceId[] = ['tb303-a', 'tb303-b', 'tr808', 'tr909'];
  deviceOrder.forEach((deviceId, deviceIndex) => {
    const steps = stepsForDevice(song, deviceId);
    if (!steps) return;

    const isDrum = deviceId === 'tr808' || deviceId === 'tr909';
    const channel = isDrum ? drumChannel : deviceIndex;
    const events = isDrum
      ? buildDrumEvents(steps, channel, repeats)
      : buildAcidEvents(steps, channel, repeats, slideMode);

    if (events.length === 0) return;
    tracks.push(buildTrack(DEVICE_TRACK_NAMES[deviceId], events));
  });

  // Header: format 1, track count, ticks per quarter.
  const header = chunk('MThd', [...be16(1), ...be16(tracks.length), ...be16(TICKS_PER_QUARTER)]);

  const total = header.length + tracks.reduce((sum, t) => sum + t.length, 0);
  const out = new Uint8Array(total);
  out.set(header, 0);
  let offset = header.length;
  for (const track of tracks) {
    out.set(track, offset);
    offset += track.length;
  }
  return out;
}
