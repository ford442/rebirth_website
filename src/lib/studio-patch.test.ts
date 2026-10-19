import { describe, expect, it } from 'vitest';
import { deflateSync } from 'fflate';
import {
  MAX_PATCH_BYTES,
  PATCH_PARAM,
  PATCH_VERSION,
  STEP_FLAG_ACCENT,
  STEP_FLAG_ACTIVE,
  STEP_FLAG_SLIDE,
  buildShareUrl,
  decodeStudioPatch,
  emptyPatch,
  encodeStudioPatch,
  isPatchEmpty,
  readPatchFromSearch,
  type PatchStep,
  type StudioPatch,
} from './studio-patch';

function step(overrides: Partial<PatchStep> = {}): PatchStep {
  return {
    deviceIndex: 0,
    bank: 1,
    patternIndex: 2,
    stepIndex: 5,
    note: 51,
    drumExtra: 0,
    active: true,
    accent: true,
    slide: false,
    ...overrides,
  };
}

/** Wrap raw JSON in the wire encoding, bypassing the encoder's own checks. */
function craft(json: string): string {
  const bytes = deflateSync(new TextEncoder().encode(json));
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

/** Deterministic noise, so deflate cannot squeeze the oversized patch under the cap. */
function lcg(seed: number): () => number {
  let state = seed;
  return () => {
    state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

describe('round trip', () => {
  it('survives deflate + Base64URL with bpm, steps and params', () => {
    const patch: StudioPatch = {
      version: PATCH_VERSION,
      bpm: 138,
      steps: [step()],
      params: [{ deviceIndex: 0, paramId: 1, value: 0.75 }],
    };
    const encoded = encodeStudioPatch(patch);

    // Base64URL only — nothing a query string would have to escape.
    expect(encoded).toMatch(/^[A-Za-z0-9_-]+$/);
    expect(decodeStudioPatch(encoded)).toEqual(patch);
  });

  it('round-trips an empty patch', () => {
    const decoded = decodeStudioPatch(encodeStudioPatch(emptyPatch()));
    expect(decoded).toEqual(emptyPatch());
    expect(isPatchEmpty(decoded!)).toBe(true);
  });

  it('packs the three step flags independently', () => {
    const flags = [
      { active: true, accent: false, slide: false },
      { active: false, accent: true, slide: false },
      { active: false, accent: false, slide: true },
      { active: true, accent: true, slide: true },
      { active: false, accent: false, slide: false },
    ];
    const patch = { ...emptyPatch(), steps: flags.map((f, i) => step({ stepIndex: i, ...f })) };
    const decoded = decodeStudioPatch(encodeStudioPatch(patch))!;
    expect(decoded.steps.map(({ active, accent, slide }) => ({ active, accent, slide }))).toEqual(
      flags
    );
    expect([STEP_FLAG_ACTIVE, STEP_FLAG_ACCENT, STEP_FLAG_SLIDE]).toEqual([1, 2, 4]);
  });

  it('rounds knob values to three decimals and the tempo to a whole BPM', () => {
    const patch: StudioPatch = {
      version: PATCH_VERSION,
      bpm: 137.6,
      steps: [],
      params: [{ deviceIndex: 1, paramId: 2, value: 0.123456 }],
    };
    const decoded = decodeStudioPatch(encodeStudioPatch(patch))!;
    expect(decoded.bpm).toBe(138);
    expect(decoded.params[0].value).toBe(0.123);
  });

  it('clamps step fields to a byte and drops what that pushes out of range', () => {
    const patch = {
      ...emptyPatch(),
      steps: [step({ note: 300 }), step({ note: -4 }), step({ deviceIndex: 300 })],
    };
    const decoded = decodeStudioPatch(encodeStudioPatch(patch))!;
    // 300 → 255, -4 → 0; device 300 → 255 is not one of the four devices.
    expect(decoded.steps.map((s) => s.note)).toEqual([255, 0]);
  });

  it('omits a non-finite tempo instead of writing null', () => {
    const decoded = decodeStudioPatch(
      encodeStudioPatch({ ...emptyPatch(), bpm: Number.NaN, steps: [step()] })
    )!;
    expect(decoded.bpm).toBeUndefined();
  });
});

describe('decode: corrupt input', () => {
  it.each([
    ['empty string', ''],
    ['characters outside Base64URL', 'not-a-real-patch-!!'],
    ['Base64 that is not deflate', btoa('hello, world')],
    ['a single stray character', 'A'],
  ])('%s → null', (_name, encoded) => {
    expect(decodeStudioPatch(encoded)).toBeNull();
  });

  it('returns null for a truncated payload', () => {
    const encoded = encodeStudioPatch({ ...emptyPatch(), steps: [step(), step({ stepIndex: 6 })] });
    expect(decodeStudioPatch(encoded.slice(0, Math.floor(encoded.length / 2)))).toBeNull();
  });

  it.each([
    ['not JSON', 'this is not json'],
    ['JSON null', 'null'],
    ['a JSON array', '[1,2,3]'],
    ['a JSON number', '42'],
    ['a JSON string', '"v"'],
  ])('deflate that decodes to %s → null', (_name, json) => {
    expect(decodeStudioPatch(craft(json))).toBeNull();
  });
});

describe('decode: version skew', () => {
  it.each([
    ['a newer version', `{"v":${PATCH_VERSION + 1}}`],
    ['an older version', `{"v":${PATCH_VERSION - 1}}`],
    ['no version', '{"b":120}'],
    ['a version that is a string', `{"v":"${PATCH_VERSION}"}`],
  ])('%s is refused rather than half-applied', (_name, json) => {
    expect(decodeStudioPatch(craft(json))).toBeNull();
  });

  it('ignores fields it does not know, so additive changes stay readable', () => {
    const decoded = decodeStudioPatch(craft(`{"v":${PATCH_VERSION},"b":99,"future":[1,2,3]}`));
    expect(decoded).toEqual({ version: PATCH_VERSION, bpm: 99, steps: [], params: [] });
  });
});

describe('decode: hostile or hand-edited tuples', () => {
  const wire = (body: string) => craft(`{"v":${PATCH_VERSION},${body}}`);

  it('skips step tuples that are short, not arrays, or aim outside the grid', () => {
    const decoded = decodeStudioPatch(
      wire(
        '"s":[[0,0,0,0,60,0,1],[0,0,0,0],"junk",null,[4,0,0,0,60,0,1],[0,0,0,16,60,0,1],[0,0,0,-1,60,0,1],[3,0,0,15,60,0,1]]'
      )
    )!;
    expect(decoded.steps.map((s) => [s.deviceIndex, s.stepIndex])).toEqual([
      [0, 0],
      [3, 15],
    ]);
  });

  it('skips knob tuples that are short, off-device or not finite numbers', () => {
    const decoded = decodeStudioPatch(
      wire('"k":[[0,1,0.5],[0,1],[9,1,0.5],[0,1,"loud"],[0,1,null],[2,3,1]]')
    )!;
    expect(decoded.params).toEqual([
      { deviceIndex: 0, paramId: 1, value: 0.5 },
      { deviceIndex: 2, paramId: 3, value: 1 },
    ]);
  });

  it('skips step tuples whose fields are not numbers', () => {
    const decoded = decodeStudioPatch(
      wire('"s":[["a",0,0,0,60,0,1],[0,0,0,0,"60",0,1],[0,0,0,0,60,0,1]]')
    )!;
    expect(decoded.steps).toHaveLength(1);
  });

  it.each([
    ['steps as an object', '"s":{"0":1}'],
    ['steps as a number', '"s":5'],
    ['knobs as a string', '"k":"boom"'],
    ['knobs as an object', '"k":{}'],
  ])('%s decodes to an empty list instead of throwing', (_name, body) => {
    const decoded = decodeStudioPatch(wire(body));
    expect(decoded).toEqual({ version: PATCH_VERSION, steps: [], params: [] });
  });

  it('ignores a tempo that is not a number', () => {
    expect(decodeStudioPatch(wire('"b":"fast"'))!.bpm).toBeUndefined();
  });
});

describe('buildShareUrl', () => {
  it('layers ?p= onto ?play= and keeps the base path', () => {
    const result = buildShareUrl(
      'https://ford442.github.io/rebirth_website/play?play=demo%2Fsong.rbs',
      { ...emptyPatch(), steps: [step({ deviceIndex: 2, bank: 0, patternIndex: 0, stepIndex: 0 })] }
    );

    expect(result.ok).toBe(true);
    const parsed = new URL(result.url!);
    expect(parsed.pathname).toBe('/rebirth_website/play');
    expect(parsed.searchParams.get('play')).toBe('demo/song.rbs');
    expect(parsed.searchParams.get(PATCH_PARAM)).toBeTruthy();
    expect(result.size).toBe(parsed.searchParams.get(PATCH_PARAM)!.length);
  });

  it('replaces an existing ?p= rather than appending a second one', () => {
    const first = buildShareUrl('https://example.test/play?src=a.rbs', {
      ...emptyPatch(),
      bpm: 100,
    }).url!;
    const second = buildShareUrl(first, { ...emptyPatch(), bpm: 150 }).url!;
    const params = new URL(second).searchParams;
    expect(params.getAll(PATCH_PARAM)).toHaveLength(1);
    expect(params.get('src')).toBe('a.rbs');
    expect(decodeStudioPatch(params.get(PATCH_PARAM)!)!.bpm).toBe(150);
  });

  it('refuses an empty patch', () => {
    expect(buildShareUrl('https://example.test/play', emptyPatch())).toEqual({
      ok: false,
      size: 0,
      reason: 'empty',
    });
  });

  it('refuses an oversized patch rather than truncating it into a link', () => {
    const random = lcg(1);
    const steps = Array.from({ length: 4000 }, (_, i) =>
      step({
        deviceIndex: i % 4,
        bank: i % 4,
        patternIndex: i % 8,
        stepIndex: i % 16,
        note: Math.floor(random() * 128),
        drumExtra: Math.floor(random() * 8),
        accent: i % 3 === 0,
        slide: i % 5 === 0,
      })
    );
    const result = buildShareUrl('https://example.test/rebirth_website/play', {
      ...emptyPatch(),
      steps,
    });

    expect(result.ok).toBe(false);
    expect(result.reason).toBe('too-large');
    expect(result.size).toBeGreaterThan(MAX_PATCH_BYTES);
    expect(result.url).toBeUndefined();
  });

  it('accepts the largest patch that fits and refuses the next one up', () => {
    const patchOf = (count: number): StudioPatch => {
      const random = lcg(7);
      return {
        ...emptyPatch(),
        steps: Array.from({ length: count }, (_, i) =>
          step({ stepIndex: i % 16, note: Math.floor(random() * 128) })
        ),
      };
    };
    let fits = 0;
    while (encodeStudioPatch(patchOf(fits + 1)).length <= MAX_PATCH_BYTES) fits += 1;

    expect(fits).toBeGreaterThan(100);
    expect(buildShareUrl('https://example.test/play', patchOf(fits)).ok).toBe(true);
    expect(buildShareUrl('https://example.test/play', patchOf(fits + 1)).reason).toBe('too-large');
  });
});

describe('readPatchFromSearch', () => {
  it('reads a patch with or without the leading ?', () => {
    const encoded = encodeStudioPatch({ ...emptyPatch(), bpm: 120 });
    expect(readPatchFromSearch(`?play=a.rbs&p=${encoded}`)!.bpm).toBe(120);
    expect(readPatchFromSearch(`p=${encoded}`)!.bpm).toBe(120);
  });

  it('returns null when ?p= is absent, empty or corrupt', () => {
    expect(readPatchFromSearch('')).toBeNull();
    expect(readPatchFromSearch('?play=a.rbs')).toBeNull();
    expect(readPatchFromSearch('?p=')).toBeNull();
    expect(readPatchFromSearch('?p=%%%')).toBeNull();
    expect(readPatchFromSearch('?p=definitely-not-a-patch')).toBeNull();
  });
});
