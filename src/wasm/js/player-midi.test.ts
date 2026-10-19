import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  ACCENT_VELOCITY,
  CC_CUTOFF,
  CC_RESONANCE,
  DRUM_NOTE_MAP,
  createMidiInput,
  describePorts,
  isWebMidiSupported,
  mapMidiMessage,
  type MidiAction,
  type MidiState,
} from './player-midi';
import { DeviceParam } from './player-studio';

const map = (...bytes: number[]) => mapMidiMessage(Uint8Array.from(bytes));

describe('mapMidiMessage: TB-303 notes', () => {
  it('channel 1 addresses 303 A and channel 2 addresses 303 B', () => {
    expect(map(0x90, 60, 64)).toEqual({
      kind: 'acid-note',
      device: 'tb303-a',
      note: 60,
      accent: false,
      on: true,
    });
    expect(map(0x91, 62, 64)).toEqual({
      kind: 'acid-note',
      device: 'tb303-b',
      note: 62,
      accent: false,
      on: true,
    });
  });

  it('turns velocity into accent at the documented threshold', () => {
    expect(ACCENT_VELOCITY).toBe(100);
    expect(map(0x90, 60, ACCENT_VELOCITY - 1)).toMatchObject({ accent: false });
    expect(map(0x90, 60, ACCENT_VELOCITY)).toMatchObject({ accent: true });
    expect(map(0x91, 62, 127)).toMatchObject({ accent: true });
  });

  it('reads a real note-off (0x80) as off, with release velocity still setting accent', () => {
    expect(map(0x80, 60, 0)).toMatchObject({ kind: 'acid-note', on: false, accent: false });
    expect(map(0x81, 60, 120)).toMatchObject({ device: 'tb303-b', on: false, accent: true });
  });

  it('reads a note-on with velocity 0 as the traditional note-off', () => {
    expect(map(0x90, 60, 0)).toMatchObject({ kind: 'acid-note', on: false });
    // A two-byte message has no velocity byte at all: it is velocity 0.
    expect(map(0x90, 60)).toMatchObject({ kind: 'acid-note', on: false });
  });

  it('passes the full 0–127 note range through', () => {
    expect(map(0x90, 0, 64)).toMatchObject({ note: 0 });
    expect(map(0x90, 127, 64)).toMatchObject({ note: 127 });
  });

  it('masks stray high bits off data bytes', () => {
    // 0xbc → 0x3c (60); 0xe4 → 0x64 (100, right on the accent threshold).
    expect(map(0x90, 0xbc, 0xe4)).toMatchObject({ note: 0x3c, accent: true, on: true });
    // 0xe3 → 0x63 (99, one below it).
    expect(map(0x90, 0xbc, 0xe3)).toMatchObject({ note: 0x3c, accent: false, on: true });
  });

  it.each([2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15])(
    'ignores notes on channel %i (0-based index)',
    (channel) => {
      expect(map(0x90 | channel, 60, 100)).toBeNull();
      expect(map(0x80 | channel, 60, 0)).toBeNull();
    }
  );
});

describe('mapMidiMessage: drum pads on channel 10', () => {
  it('maps the kick, with velocity setting accent', () => {
    expect(map(0x99, 36, 100)).toEqual({ kind: 'drum-hit', drumKey: 'bd', accent: true, on: true });
    expect(map(0x99, 42, 40)).toEqual({ kind: 'drum-hit', drumKey: 'ch', accent: false, on: true });
  });

  it('reads note-off and zero-velocity note-on as the pad release', () => {
    expect(map(0x89, 36, 0)).toMatchObject({ kind: 'drum-hit', on: false });
    expect(map(0x99, 36, 0)).toMatchObject({ kind: 'drum-hit', on: false });
  });

  it.each(Object.entries(DRUM_NOTE_MAP))('GM note %s → %s', (note, drumKey) => {
    expect(map(0x99, Number(note), 90)).toMatchObject({ kind: 'drum-hit', drumKey });
  });

  it('accepts both the GM key and the one below it for the common pads', () => {
    expect(DRUM_NOTE_MAP[35]).toBe(DRUM_NOTE_MAP[36]);
    expect(DRUM_NOTE_MAP[40]).toBe(DRUM_NOTE_MAP[38]);
  });

  it('ignores pads with no ReBirth voice', () => {
    expect(map(0x99, 44, 100)).toBeNull(); // pedal hi-hat
    expect(map(0x99, 49, 100)).toBeNull(); // crash
    expect(map(0x99, 0, 100)).toBeNull();
  });

  it('does not let a drum note reach the 303s, nor a 303 channel reach the drums', () => {
    expect(map(0x90, 36, 100)).toMatchObject({ kind: 'acid-note' });
    expect(map(0x99, 36, 100)).toMatchObject({ kind: 'drum-hit' });
  });

  it('keeps its map immutable', () => {
    expect(Object.isFrozen(DRUM_NOTE_MAP)).toBe(true);
  });
});

describe('mapMidiMessage: control change', () => {
  it('maps CC 74 to cutoff and CC 71 to resonance', () => {
    expect(CC_CUTOFF).toBe(74);
    expect(CC_RESONANCE).toBe(71);
    expect(map(0xb0, 74, 127)).toEqual({
      kind: 'param',
      paramId: DeviceParam.Cutoff,
      value: 1,
      label: 'CUTOFF',
    });
    expect(map(0xb0, 71, 0)).toEqual({
      kind: 'param',
      paramId: DeviceParam.Resonance,
      value: 0,
      label: 'RESO',
    });
  });

  it('pins the engine ids the studio sends (DeviceParamId 1 and 2)', () => {
    expect(DeviceParam.Cutoff).toBe(1);
    expect(DeviceParam.Resonance).toBe(2);
  });

  it('normalises 0–127 onto 0–1', () => {
    const value = (cc: number) =>
      (map(0xb0, 74, cc) as Extract<MidiAction, { kind: 'param' }>).value;
    expect(value(0)).toBe(0);
    expect(value(127)).toBe(1);
    expect(value(64)).toBeCloseTo(64 / 127, 10);
  });

  it('answers on any channel — knobs are not tied to the note channels', () => {
    for (let channel = 0; channel < 16; channel += 1) {
      expect(map(0xb0 | channel, 74, 10)).toMatchObject({ kind: 'param', label: 'CUTOFF' });
    }
  });

  it.each([0, 1, 7, 10, 64, 70, 72, 73, 75, 120, 127])('ignores CC %i', (cc) => {
    expect(map(0xb0, cc, 90)).toBeNull();
  });

  it('treats a CC with no value byte as 0', () => {
    expect(map(0xb0, 74)).toMatchObject({ kind: 'param', value: 0 });
  });
});

describe('mapMidiMessage: everything else is ignored', () => {
  it.each([
    ['poly aftertouch', [0xa0, 60, 80]],
    ['program change', [0xc0, 5]],
    ['channel pressure', [0xd0, 80]],
    ['pitch bend', [0xe0, 0, 64]],
    ['sysex start', [0xf0, 0x7e, 0x7f]],
    ['time code', [0xf1, 0x00]],
    ['song position', [0xf2, 0x00, 0x00]],
    ['timing clock', [0xf8]],
    ['start', [0xfa]],
    ['continue', [0xfb]],
    ['stop', [0xfc]],
    ['active sensing', [0xfe]],
    ['system reset', [0xff]],
  ])('%s', (_name, bytes) => {
    expect(map(...bytes)).toBeNull();
  });

  it.each([
    ['an empty message', []],
    ['a status byte alone', [0x90]],
    ['running status (data byte first)', [0x3c, 0x40]],
    ['a zero byte', [0x00, 0x40]],
  ])('%s', (_name, bytes) => {
    expect(map(...bytes)).toBeNull();
  });

  it('accepts any ArrayLike, not just Uint8Array', () => {
    expect(mapMidiMessage([0x90, 60, 100])).toMatchObject({ kind: 'acid-note', accent: true });
  });
});

describe('describePorts', () => {
  it('summarises zero, one and several inputs', () => {
    expect(describePorts([])).toBe('No MIDI inputs found');
    expect(describePorts(['Launchpad'])).toBe('Launchpad');
    expect(describePorts(['Launchpad', 'Keystation', 'nanoKONTROL'])).toBe('Launchpad +2');
  });
});

// ── createMidiInput ──────────────────────────────────────────────

interface FakePort {
  name: string | null;
  onmidimessage: ((event: { data: Uint8Array | null }) => void) | null;
}

function installMidi(request: () => Promise<unknown>) {
  vi.stubGlobal('navigator', { requestMIDIAccess: request });
}

function fakeAccess(ports: FakePort[]) {
  return {
    inputs: new Map(ports.map((port, i) => [`in-${i}`, port])),
    onstatechange: null as (() => void) | null,
  };
}

function harness() {
  const states: Array<[MidiState, string]> = [];
  const actions: MidiAction[] = [];
  const input = createMidiInput({
    onAction: (a) => actions.push(a),
    onState: (s, d) => states.push([s, d]),
  });
  return { input, states, actions };
}

describe('createMidiInput', () => {
  afterEach(() => vi.unstubAllGlobals());

  it('reports unsupported where the API is missing', async () => {
    vi.stubGlobal('navigator', {});
    expect(isWebMidiSupported()).toBe(false);

    const { input, states } = harness();
    expect(input.supported).toBe(false);
    expect(input.state).toBe('unsupported');
    await input.connect();
    expect(states.at(-1)).toEqual(['unsupported', 'Web MIDI is not available in this browser']);
  });

  it('connects, decodes messages from every port and names the inputs', async () => {
    const pad: FakePort = { name: 'Launchpad', onmidimessage: null };
    const keys: FakePort = { name: null, onmidimessage: null };
    installMidi(() => Promise.resolve(fakeAccess([pad, keys])));

    const { input, states, actions } = harness();
    expect(input.state).toBe('idle');
    await input.connect();

    expect(input.state).toBe('connected');
    expect(states.map(([s]) => s)).toEqual(['connecting', 'connected']);
    expect(states.at(-1)![1]).toBe('Launchpad +1');

    pad.onmidimessage!({ data: Uint8Array.from([0x99, 36, 100]) });
    keys.onmidimessage!({ data: Uint8Array.from([0x90, 60, 64]) });
    keys.onmidimessage!({ data: null });
    keys.onmidimessage!({ data: Uint8Array.from([0xf8]) });
    expect(actions.map((a) => a.kind)).toEqual(['drum-hit', 'acid-note']);
  });

  it('is idle, not connected, when no inputs are plugged in', async () => {
    installMidi(() => Promise.resolve(fakeAccess([])));
    const { input, states } = harness();
    await input.connect();
    expect(input.state).toBe('idle');
    expect(states.at(-1)).toEqual(['idle', 'No MIDI inputs found']);
  });

  it('asks for access only once, however often connect() is called', async () => {
    const request = vi.fn(() => Promise.resolve(fakeAccess([{ name: 'A', onmidimessage: null }])));
    installMidi(request);
    const { input } = harness();
    await input.connect();
    await input.connect();
    expect(request).toHaveBeenCalledTimes(1);
  });

  it('rebinds when a port is plugged in later', async () => {
    const ports: FakePort[] = [];
    const access = fakeAccess(ports);
    installMidi(() => Promise.resolve(access));
    const { input, states } = harness();
    await input.connect();
    expect(input.state).toBe('idle');

    const late: FakePort = { name: 'Late', onmidimessage: null };
    access.inputs.set('late', late);
    access.onstatechange!();
    expect(input.state).toBe('connected');
    expect(states.at(-1)![1]).toBe('Late');
    expect(late.onmidimessage).toBeTypeOf('function');
  });

  it('reports a refused permission prompt as denied', async () => {
    for (const name of ['SecurityError', 'NotAllowedError']) {
      installMidi(() => Promise.reject(new DOMException('no', name)));
      const { input, states } = harness();
      await input.connect();
      expect(input.state).toBe('denied');
      expect(states.at(-1)).toEqual(['denied', 'MIDI access denied']);
    }
  });

  it('reports any other failure as an error with its message', async () => {
    installMidi(() => Promise.reject(new Error('driver exploded')));
    const { input, states } = harness();
    await input.connect();
    expect(input.state).toBe('error');
    expect(states.at(-1)).toEqual(['error', 'driver exploded']);
  });

  it('can try again after a failure', async () => {
    const request = vi
      .fn<() => Promise<unknown>>()
      .mockRejectedValueOnce(new Error('first time unlucky'))
      .mockResolvedValueOnce(fakeAccess([{ name: 'A', onmidimessage: null }]));
    installMidi(request);
    const { input } = harness();
    await input.connect();
    expect(input.state).toBe('error');
    await input.connect();
    expect(input.state).toBe('connected');
  });

  it('disconnect() drops the port handlers and returns to idle', async () => {
    const port: FakePort = { name: 'A', onmidimessage: null };
    const access = fakeAccess([port]);
    installMidi(() => Promise.resolve(access));
    const { input, states } = harness();
    await input.connect();
    expect(port.onmidimessage).toBeTypeOf('function');

    input.disconnect();
    expect(port.onmidimessage).toBeNull();
    expect(access.onstatechange).toBeNull();
    expect(input.state).toBe('idle');
    expect(states.at(-1)).toEqual(['idle', 'MIDI off']);
  });
});
