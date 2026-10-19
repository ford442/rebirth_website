import { describe, expect, it } from 'vitest';
import type { DeviceId, ParsedSong, Pattern, StepData } from '../wasm/types/wasm-audio-song';
import {
  GM_DRUM_MAP,
  TICKS_PER_QUARTER,
  TICKS_PER_STEP,
  songToMidi,
  writeVarLength,
} from './midi-smf';

// ── Fixtures ─────────────────────────────────────────────────────

const off: StepData = { active: false, accent: false, slide: false };

function acid(note: number, extra: Partial<StepData> = {}): StepData {
  return { active: true, accent: false, slide: false, note, ...extra };
}

function drum(hits: number, extra: Partial<StepData> = {}): StepData {
  return { active: true, accent: false, slide: false, note: hits, drumExtra: 0, ...extra };
}

function pattern(deviceId: DeviceId, steps: StepData[], bank = 0, patternIndex = 0): Pattern {
  return { deviceId, bank, patternIndex, steps };
}

function song(patterns: Pattern[], overrides: Partial<ParsedSong> = {}): ParsedSong {
  const deviceIds: DeviceId[] = ['tb303-a', 'tb303-b', 'tr808', 'tr909'];
  return {
    title: 'T',
    author: '',
    bpm: 120,
    devices: deviceIds.map((deviceId) => ({
      deviceId,
      knobs: {},
      muted: false,
      level: 1,
      pan: 0.5,
      waveform: 0,
      initialPatternBank: 0,
      initialPatternIndex: 0,
    })),
    patterns,
    arrangement: [],
    ...overrides,
  };
}

// ── A just-enough SMF reader ─────────────────────────────────────
//
// Parses what the writer emits (no running status, no sysex), so the tests
// can assert on events rather than on offsets.

type SmfEvent =
  | { tick: number; kind: 'meta'; type: number; data: number[] }
  | { tick: number; kind: 'note-on' | 'note-off'; channel: number; key: number; value: number }
  | { tick: number; kind: 'cc'; channel: number; controller: number; value: number };

interface SmfTrack {
  declaredLength: number;
  actualLength: number;
  events: SmfEvent[];
  name: string;
}

interface Smf {
  format: number;
  trackCount: number;
  division: number;
  tracks: SmfTrack[];
}

function readSmf(bytes: Uint8Array): Smf {
  const u32 = (at: number) =>
    ((bytes[at] << 24) | (bytes[at + 1] << 16) | (bytes[at + 2] << 8) | bytes[at + 3]) >>> 0;
  const u16 = (at: number) => (bytes[at] << 8) | bytes[at + 1];
  const tag = (at: number) => String.fromCharCode(...bytes.slice(at, at + 4));

  expect(tag(0)).toBe('MThd');
  expect(u32(4)).toBe(6);
  const smf: Smf = { format: u16(8), trackCount: u16(10), division: u16(12), tracks: [] };

  let at = 14;
  while (at < bytes.length) {
    expect(tag(at)).toBe('MTrk');
    const declaredLength = u32(at + 4);
    const bodyStart = at + 8;
    const bodyEnd = bodyStart + declaredLength;
    const events: SmfEvent[] = [];
    let name = '';
    let tick = 0;
    let p = bodyStart;

    const varLength = () => {
      let value = 0;
      for (;;) {
        const byte = bytes[p++];
        value = (value << 7) | (byte & 0x7f);
        if ((byte & 0x80) === 0) return value;
      }
    };

    while (p < bodyEnd) {
      tick += varLength();
      const status = bytes[p++];
      if (status === 0xff) {
        const type = bytes[p++];
        const length = varLength();
        const data = Array.from(bytes.slice(p, p + length));
        p += length;
        if (type === 0x03) name = String.fromCharCode(...data);
        events.push({ tick, kind: 'meta', type, data });
      } else {
        const channel = status & 0x0f;
        const high = status & 0xf0;
        const first = bytes[p++];
        const second = bytes[p++];
        if (high === 0x90) {
          events.push({ tick, kind: 'note-on', channel, key: first, value: second });
        } else if (high === 0x80) {
          events.push({ tick, kind: 'note-off', channel, key: first, value: second });
        } else if (high === 0xb0) {
          events.push({ tick, kind: 'cc', channel, controller: first, value: second });
        } else {
          throw new Error(`unexpected status 0x${status.toString(16)}`);
        }
      }
    }
    expect(p).toBe(bodyEnd);
    smf.tracks.push({ declaredLength, actualLength: bodyEnd - bodyStart, events, name });
    at = bodyEnd;
  }
  return smf;
}

const hex = (bytes: ArrayLike<number>) =>
  Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(' ');

/** The note events of one track, as `[tick, kind, key, channel]` rows. */
function notes(track: SmfTrack) {
  return track.events
    .filter((e) => e.kind === 'note-on' || e.kind === 'note-off')
    .map((e) => {
      if (e.kind !== 'note-on' && e.kind !== 'note-off') throw new Error('unreachable');
      return [e.tick, e.kind, e.key, e.channel] as const;
    });
}

function trackNamed(smf: Smf, name: string): SmfTrack {
  const track = smf.tracks.find((t) => t.name === name);
  if (!track) throw new Error(`no track named ${name}`);
  return track;
}

// ── Tests ────────────────────────────────────────────────────────

describe('constants', () => {
  it('uses 96 ticks per quarter, so a 16th step is 24 ticks', () => {
    expect(TICKS_PER_QUARTER).toBe(96);
    expect(TICKS_PER_STEP).toBe(24);
  });
});

describe('writeVarLength', () => {
  it.each([
    [0, '00'],
    [1, '01'],
    [0x7f, '7f'],
    [0x80, '81 00'],
    [0x2000, 'c0 00'],
    [0x3fff, 'ff 7f'],
    [0x4000, '81 80 00'],
    [0x1fffff, 'ff ff 7f'],
    [0x200000, '81 80 80 00'],
    [0x0fffffff, 'ff ff ff 7f'],
  ])('%i → %s (the SMF spec table)', (value, expected) => {
    expect(hex(writeVarLength(value))).toBe(expected);
  });

  it('clamps negatives to 0 and drops the fraction', () => {
    expect(writeVarLength(-5)).toEqual([0]);
    expect(writeVarLength(127.9)).toEqual([0x7f]);
  });
});

describe('songToMidi: byte golden', () => {
  it('writes exactly these bytes for one accented 303 note at 120 BPM', () => {
    const bytes = songToMidi(song([pattern('tb303-a', [acid(0x24, { accent: true }), off])]));

    expect(hex(bytes)).toBe(
      [
        // MThd: length 6, format 1, 2 tracks, 96 ticks per quarter
        '4d 54 68 64 00 00 00 06 00 01 00 02 00 60',
        // MTrk 0 (length 0x18): name "T", tempo 500000 µs/qn, 4/4, end of track
        '4d 54 72 6b 00 00 00 18',
        '00 ff 03 01 54',
        '00 ff 51 03 07 a1 20',
        '00 ff 58 04 04 02 18 08',
        '00 ff 2f 00',
        // MTrk 1 (length 0x18): "TB-303 A", note-on key 0x24 vel 0x70 at 0, note-off at +24
        '4d 54 72 6b 00 00 00 18',
        '00 ff 03 08 54 42 2d 33 30 33 20 41',
        '00 90 24 70',
        '18 80 24 40',
        '00 ff 2f 00',
      ].join(' ')
    );
  });
});

describe('songToMidi: structure', () => {
  const full = song([
    pattern('tb303-a', [acid(36), off, acid(48), off]),
    pattern('tb303-b', [off, acid(40), off, acid(52)]),
    pattern('tr808', [drum(0x01), off, drum(0x02), off]),
    pattern('tr909', [drum(0x20), drum(0x20), drum(0x20), drum(0x20)]),
  ]);

  it('is a Type 1 file: a tempo track plus one track per playing device', () => {
    const smf = readSmf(songToMidi(full));
    expect(smf.format).toBe(1);
    expect(smf.division).toBe(96);
    expect(smf.trackCount).toBe(5);
    expect(smf.tracks.map((t) => t.name)).toEqual([
      'T',
      'TB-303 A',
      'TB-303 B',
      'TR-808',
      'TR-909',
    ]);
  });

  it('declares each track length correctly and ends each with End of Track', () => {
    const smf = readSmf(songToMidi(full));
    for (const track of smf.tracks) {
      expect(track.declaredLength).toBe(track.actualLength);
      expect(track.events.at(-1)).toMatchObject({ kind: 'meta', type: 0x2f, data: [] });
    }
  });

  it('leaves out devices that have no pattern or no active steps', () => {
    const smf = readSmf(
      songToMidi(song([pattern('tb303-a', [acid(36)]), pattern('tr808', [off, off])]))
    );
    expect(smf.trackCount).toBe(2);
    expect(smf.tracks.map((t) => t.name)).toEqual(['T', 'TB-303 A']);
  });

  it('still writes a valid tempo-only file for a song with no patterns', () => {
    const smf = readSmf(songToMidi(song([])));
    expect(smf.trackCount).toBe(1);
    expect(smf.tracks).toHaveLength(1);
  });

  it('names the tempo track after the song, with a house fallback', () => {
    expect(readSmf(songToMidi(song([], { title: 'Acid Rain' }))).tracks[0].name).toBe('Acid Rain');
    expect(readSmf(songToMidi(song([], { title: '' }))).tracks[0].name).toBe('ReBirth RB-338');
  });

  it('uses the pattern each device is sitting on, falling back to its first', () => {
    const picked = song(
      [pattern('tb303-a', [acid(10)], 0, 0), pattern('tb303-a', [acid(20)], 2, 3)],
      {
        devices: song([]).devices.map((d) =>
          d.deviceId === 'tb303-a' ? { ...d, initialPatternBank: 2, initialPatternIndex: 3 } : d
        ),
      }
    );
    expect(notes(trackNamed(readSmf(songToMidi(picked)), 'TB-303 A'))[0][2]).toBe(20);

    const fallback = song([pattern('tb303-a', [acid(10)], 0, 0)], {
      devices: song([]).devices.map((d) =>
        d.deviceId === 'tb303-a' ? { ...d, initialPatternBank: 3, initialPatternIndex: 7 } : d
      ),
    });
    expect(notes(trackNamed(readSmf(songToMidi(fallback)), 'TB-303 A'))[0][2]).toBe(10);
  });
});

describe('songToMidi: tempo and time signature', () => {
  const tempoTrack = (options: Parameters<typeof songToMidi>[1], bpm = 120) =>
    readSmf(songToMidi(song([], { bpm }), options)).tracks[0];
  const tempoOf = (track: SmfTrack) =>
    track.events.find((e) => e.kind === 'meta' && e.type === 0x51);

  it.each([
    [120, [0x07, 0xa1, 0x20]], // 500000
    [140, [0x06, 0x8a, 0x1b]], // 428571
    [60, [0x0f, 0x42, 0x40]], // 1000000
    [125, [0x07, 0x53, 0x00]], // 480000
  ])('%i BPM → set-tempo %j', (bpm, data) => {
    expect(tempoOf(tempoTrack(undefined, bpm))).toMatchObject({ data });
  });

  it('lets options.bpm override the song tempo', () => {
    expect(tempoOf(tempoTrack({ bpm: 60 }, 200))).toMatchObject({ data: [0x0f, 0x42, 0x40] });
  });

  it('writes 4/4 with 24 clocks per click', () => {
    const time = tempoTrack(undefined).events.find((e) => e.kind === 'meta' && e.type === 0x58);
    expect(time).toMatchObject({ data: [4, 2, 24, 8] });
  });
});

describe('songToMidi: 303 notes', () => {
  it('puts accented notes at velocity 112 and plain ones at 80', () => {
    const smf = readSmf(
      songToMidi(song([pattern('tb303-a', [acid(36, { accent: true }), acid(36)])]))
    );
    const velocities = trackNamed(smf, 'TB-303 A')
      .events.filter((e) => e.kind === 'note-on')
      .map((e) => (e.kind === 'note-on' ? e.value : -1));
    expect(velocities).toEqual([112, 80]);
  });

  it('sends the first 303 on channel 0 and the second on channel 1', () => {
    const smf = readSmf(
      songToMidi(song([pattern('tb303-a', [acid(36)]), pattern('tb303-b', [acid(37)])]))
    );
    expect(notes(trackNamed(smf, 'TB-303 A')).map((n) => n[3])).toEqual([0, 0]);
    expect(notes(trackNamed(smf, 'TB-303 B')).map((n) => n[3])).toEqual([1, 1]);
  });

  it('clamps notes into 0–127 and treats a missing note as 0', () => {
    const smf = readSmf(
      songToMidi(
        song([
          pattern('tb303-a', [acid(200), acid(-5), { active: true, accent: false, slide: false }]),
        ])
      )
    );
    const keys = notes(trackNamed(smf, 'TB-303 A'))
      .filter((n) => n[1] === 'note-on')
      .map((n) => n[2]);
    expect(keys).toEqual([127, 0, 0]);
  });

  it('releases a note before the next one starts on the same tick, so repeats retrigger', () => {
    const smf = readSmf(songToMidi(song([pattern('tb303-a', [acid(36), acid(36)])])));
    const atTick24 = notes(trackNamed(smf, 'TB-303 A')).filter((n) => n[0] === TICKS_PER_STEP);
    expect(atTick24.map((n) => n[1])).toEqual(['note-off', 'note-on']);
  });

  it('repeats the pattern once per bar', () => {
    const steps = [acid(36), off, off, off];
    const smf = readSmf(songToMidi(song([pattern('tb303-a', steps)]), { bars: 3 }));
    const onTicks = notes(trackNamed(smf, 'TB-303 A'))
      .filter((n) => n[1] === 'note-on')
      .map((n) => n[0]);
    const barTicks = steps.length * TICKS_PER_STEP;
    expect(onTicks).toEqual([0, barTicks, 2 * barTicks]);
  });

  it.each([[undefined], [0], [-3], [0.4]])('bars=%s still writes one bar', (bars) => {
    const smf = readSmf(songToMidi(song([pattern('tb303-a', [acid(36)])]), { bars }));
    expect(notes(trackNamed(smf, 'TB-303 A')).filter((n) => n[1] === 'note-on')).toHaveLength(1);
  });
});

describe('songToMidi: slides', () => {
  const slid = song([pattern('tb303-a', [acid(36, { slide: true }), acid(39), off, off])]);

  it('overlap mode (default) holds a slid note one tick into the next step', () => {
    const track = trackNamed(readSmf(songToMidi(slid)), 'TB-303 A');
    const firstOff = notes(track).find((n) => n[1] === 'note-off' && n[2] === 36)!;
    expect(firstOff[0]).toBe(TICKS_PER_STEP + 1);
    expect(track.events.some((e) => e.kind === 'cc')).toBe(false);
  });

  it('does not stretch a slide into a rest', () => {
    const song2 = song([pattern('tb303-a', [acid(36, { slide: true }), off, off, off])]);
    const track = trackNamed(readSmf(songToMidi(song2)), 'TB-303 A');
    expect(notes(track).find((n) => n[1] === 'note-off')![0]).toBe(TICKS_PER_STEP);
  });

  it('wraps: a slide on the last step glides into the first step of the next bar', () => {
    const wrap = song([pattern('tb303-a', [acid(40), off, off, acid(36, { slide: true })])]);
    const track = trackNamed(readSmf(songToMidi(wrap)), 'TB-303 A');
    const lastOff = notes(track).find((n) => n[1] === 'note-off' && n[2] === 36)!;
    expect(lastOff[0]).toBe(4 * TICKS_PER_STEP + 1);
  });

  it('cc65 mode brackets a slid note with portamento on and off instead', () => {
    const track = trackNamed(readSmf(songToMidi(slid, { slideMode: 'cc65' })), 'TB-303 A');
    const ccs = track.events.filter((e) => e.kind === 'cc');
    expect(ccs).toMatchObject([
      { tick: 0, controller: 65, value: 127 },
      { tick: TICKS_PER_STEP + 1, controller: 65, value: 0 },
    ]);

    // Portamento goes on before the note-on that needs it, and off after its note-off.
    const order = track.events
      .filter((e) => e.tick === 0 && (e.kind === 'cc' || e.kind === 'note-on'))
      .map((e) => e.kind);
    expect(order).toEqual(['cc', 'note-on']);
    const closing = track.events
      .filter((e) => e.tick === TICKS_PER_STEP + 1 && (e.kind === 'cc' || e.kind === 'note-off'))
      .map((e) => e.kind);
    expect(closing).toEqual(['note-off', 'cc']);
  });
});

describe('songToMidi: drums', () => {
  const drumNotes = (steps: StepData[], options?: Parameters<typeof songToMidi>[1]) =>
    notes(trackNamed(readSmf(songToMidi(song([pattern('tr808', steps)]), options)), 'TR-808'));

  it('maps every primary drum bit to its General MIDI key', () => {
    const keys = drumNotes([drum(0xff)])
      .filter((n) => n[1] === 'note-on')
      .map((n) => n[2]);
    expect(keys).toEqual([
      GM_DRUM_MAP.kick,
      GM_DRUM_MAP.snare,
      GM_DRUM_MAP.lowTom,
      GM_DRUM_MAP.midTom,
      GM_DRUM_MAP.highTom,
      GM_DRUM_MAP.closedHat,
      GM_DRUM_MAP.openHat,
      GM_DRUM_MAP.clave,
    ]);
    expect(keys).toEqual([36, 38, 45, 47, 50, 42, 46, 75]);
  });

  it('maps the secondary bits (clap, maracas, rimshot) from drumExtra', () => {
    const keys = drumNotes([drum(0, { drumExtra: 0x07 })])
      .filter((n) => n[1] === 'note-on')
      .map((n) => n[2]);
    expect(keys).toEqual([GM_DRUM_MAP.clap, GM_DRUM_MAP.maracas, GM_DRUM_MAP.rimshot]);
  });

  it('plays several hits on one step together, on the GM drum channel', () => {
    const rows = drumNotes([drum(0x03, { drumExtra: 0x01 })]);
    const ons = rows.filter((n) => n[1] === 'note-on');
    expect(ons.map((n) => [n[0], n[2], n[3]])).toEqual([
      [0, 36, 9],
      [0, 38, 9],
      [0, 39, 9],
    ]);
  });

  it('gates a hit at half a step regardless of what follows', () => {
    const rows = drumNotes([drum(0x01)]);
    expect(rows.find((n) => n[1] === 'note-off')![0]).toBe(TICKS_PER_STEP / 2);
  });

  it('honours a different drum channel and accent velocity', () => {
    const smf = readSmf(
      songToMidi(song([pattern('tr909', [drum(0x01, { accent: true })])]), { drumChannel: 3 })
    );
    const track = trackNamed(smf, 'TR-909');
    expect(notes(track).map((n) => n[3])).toEqual([3, 3]);
    expect(track.events.find((e) => e.kind === 'note-on')).toMatchObject({ value: 112 });
  });

  it('writes nothing for a step with no hits', () => {
    const smf = readSmf(songToMidi(song([pattern('tr808', [drum(0)])])));
    expect(smf.tracks.map((t) => t.name)).toEqual(['T']);
  });
});
