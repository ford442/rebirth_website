/**
 * WebMIDI input for the studio grid.
 *
 * An external controller is mapped onto the two engine calls the studio
 * already owns — `setStep` (notes and pads) and `setDeviceParam` (knobs).
 * Nothing here reaches into the engine directly: {@link mapMidiMessage} is a
 * pure decoder and {@link createMidiInput} only opens ports and hands the
 * decoded actions back, so the C++ side stays completely unaware that MIDI
 * exists.
 *
 * The mapping is deliberately fixed rather than learn-on-click:
 *
 * | Input                     | Target                                     |
 * | ------------------------- | ------------------------------------------ |
 * | Note on/off, channel 1–2  | TB-303 A/B step note (+ accent by velocity) |
 * | CC 74 / CC 71             | cutoff / resonance on the selected device   |
 * | Notes on channel 10       | 808/909 drum bits on the current step       |
 * | Clock / transport         | ignored — the engine owns tempo             |
 *
 * `navigator.requestMIDIAccess` is absent on iOS and in every non-secure
 * context, so every entry point here feature-detects and the UI hides the
 * control rather than offering a button that cannot work.
 */

import { DeviceParam } from './player-studio';
import type { DeviceId } from '../types/wasm-audio-song';

/** Velocity at or above which a note counts as accented. */
export const ACCENT_VELOCITY = 100;

/** Channel 10 (1-based) is the General MIDI drum channel. */
const DRUM_CHANNEL = 9; // 0-based

/** CC numbers the mapping listens to, per the table above. */
export const CC_CUTOFF = 74;
export const CC_RESONANCE = 71;

/**
 * General-MIDI-ish pad note → ReBirth drum key.
 *
 * Keys match `DRUM_HITS` in `player-step-edit.ts`, which in turn matches
 * `DrumHit` / `DrumExtra` in `synth/DrumBitfield.h`. Both the GM note and the
 * octave below it (where most pad controllers sit by default) are accepted.
 */
export const DRUM_NOTE_MAP: Readonly<Record<number, string>> = Object.freeze({
  35: 'bd',
  36: 'bd',
  37: 'rs',
  38: 'sd',
  39: 'cp',
  40: 'sd',
  41: 'lt',
  42: 'ch',
  43: 'lt',
  45: 'mt',
  46: 'oh',
  47: 'mt',
  48: 'ht',
  50: 'ht',
  70: 'ma',
  75: 'cl',
});

/** A decoded, device-agnostic instruction for the studio to apply. */
export type MidiAction =
  | {
      kind: 'acid-note';
      /** Channel 1 → 303 A, channel 2 → 303 B. */
      device: DeviceId;
      note: number;
      accent: boolean;
      on: boolean;
    }
  | { kind: 'drum-hit'; drumKey: string; accent: boolean; on: boolean }
  | {
      kind: 'param';
      /** Numeric `DeviceParamId`, ready for `setDeviceParam`. */
      paramId: number;
      /** Normalised 0–1. */
      value: number;
      label: string;
    };

/**
 * Decode one raw MIDI message into a studio action.
 *
 * Returns `null` for everything we deliberately ignore — clock, transport,
 * aftertouch, pitch bend, system messages and any CC outside the table.
 */
export function mapMidiMessage(data: ArrayLike<number>): MidiAction | null {
  if (data.length < 2) return null;
  const statusByte = data[0];
  if (statusByte < 0x80 || statusByte >= 0xf0) return null; // system / clock

  const status = statusByte & 0xf0;
  const channel = statusByte & 0x0f;
  const first = data[1] & 0x7f;
  const second = data.length > 2 ? data[2] & 0x7f : 0;

  if (status === 0xb0) {
    if (first === CC_CUTOFF) {
      return { kind: 'param', paramId: DeviceParam.Cutoff, value: second / 127, label: 'CUTOFF' };
    }
    if (first === CC_RESONANCE) {
      return { kind: 'param', paramId: DeviceParam.Resonance, value: second / 127, label: 'RESO' };
    }
    return null;
  }

  const isNoteOn = status === 0x90 && second > 0;
  // A note-on with velocity 0 is the traditional note-off.
  const isNoteOff = status === 0x80 || (status === 0x90 && second === 0);
  if (!isNoteOn && !isNoteOff) return null;

  if (channel === DRUM_CHANNEL) {
    const drumKey = DRUM_NOTE_MAP[first];
    if (!drumKey) return null;
    return { kind: 'drum-hit', drumKey, accent: second >= ACCENT_VELOCITY, on: isNoteOn };
  }

  if (channel === 0 || channel === 1) {
    return {
      kind: 'acid-note',
      device: channel === 0 ? 'tb303-a' : 'tb303-b',
      note: first,
      accent: second >= ACCENT_VELOCITY,
      on: isNoteOn,
    };
  }

  return null;
}

export type MidiState = 'unsupported' | 'idle' | 'connecting' | 'connected' | 'denied' | 'error';

export interface MidiInputOptions {
  /** Called for every decoded action (note-offs included — the studio decides). */
  onAction: (action: MidiAction) => void;
  /** Called whenever the connection state or the list of ports changes. */
  onState: (state: MidiState, detail: string) => void;
}

export interface MidiInput {
  /** True when this browser exposes the Web MIDI API at all. */
  readonly supported: boolean;
  /** Prompt for access and start listening. Safe to call twice. */
  connect(): Promise<void>;
  /** Stop listening and drop the port handlers. */
  disconnect(): void;
  readonly state: MidiState;
}

/** Feature-detection, kept in one place so the UI and the controller agree. */
export function isWebMidiSupported(): boolean {
  return (
    typeof navigator !== 'undefined' &&
    typeof (navigator as Navigator & { requestMIDIAccess?: unknown }).requestMIDIAccess ===
      'function'
  );
}

/** Human-readable summary of the connected inputs, for the LCD. */
export function describePorts(names: string[]): string {
  if (names.length === 0) return 'No MIDI inputs found';
  if (names.length === 1) return names[0];
  return `${names[0]} +${names.length - 1}`;
}

type MidiMessageLike = { data: Uint8Array | null };
type MidiPortLike = {
  name?: string | null;
  onmidimessage: ((event: MidiMessageLike) => void) | null;
};
type MidiAccessLike = {
  inputs: Map<string, MidiPortLike> | Iterable<[string, MidiPortLike]>;
  onstatechange: ((event: unknown) => void) | null;
};

export function createMidiInput(options: MidiInputOptions): MidiInput {
  let access: MidiAccessLike | null = null;
  let state: MidiState = isWebMidiSupported() ? 'idle' : 'unsupported';

  function setState(next: MidiState, detail: string) {
    state = next;
    options.onState(next, detail);
  }

  function portList(): MidiPortLike[] {
    if (!access) return [];
    const inputs = access.inputs;
    return Array.from(
      inputs as Iterable<[string, MidiPortLike]>,
      ([, port]: [string, MidiPortLike]) => port
    );
  }

  function bindPorts() {
    const ports = portList();
    for (const port of ports) {
      port.onmidimessage = (event: MidiMessageLike) => {
        if (!event.data) return;
        const action = mapMidiMessage(event.data);
        if (action) options.onAction(action);
      };
    }
    const names = ports.map((p) => p.name || 'MIDI input');
    setState(ports.length > 0 ? 'connected' : 'idle', describePorts(names));
  }

  return {
    get supported() {
      return isWebMidiSupported();
    },
    get state() {
      return state;
    },
    async connect() {
      if (!isWebMidiSupported()) {
        setState('unsupported', 'Web MIDI is not available in this browser');
        return;
      }
      if (access) {
        bindPorts();
        return;
      }
      setState('connecting', 'Requesting MIDI access…');
      try {
        const request = (
          navigator as Navigator & {
            requestMIDIAccess: (opts?: { sysex?: boolean }) => Promise<MidiAccessLike>;
          }
        ).requestMIDIAccess;
        access = await request.call(navigator, { sysex: false });
        access.onstatechange = () => bindPorts();
        bindPorts();
      } catch (err) {
        access = null;
        const message = err instanceof Error ? err.message : String(err);
        // A rejected permission prompt is a DOMException named SecurityError
        // or NotAllowedError depending on the browser.
        const denied =
          err instanceof DOMException &&
          (err.name === 'SecurityError' || err.name === 'NotAllowedError');
        setState(denied ? 'denied' : 'error', denied ? 'MIDI access denied' : message);
      }
    },
    disconnect() {
      for (const port of portList()) {
        port.onmidimessage = null;
      }
      if (access) access.onstatechange = null;
      access = null;
      setState(isWebMidiSupported() ? 'idle' : 'unsupported', 'MIDI off');
    },
  };
}
