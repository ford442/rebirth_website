import { test, expect } from '@playwright/test';

/**
 * WebMIDI mapping, shareable `?p=` patches and catalog fetch classification.
 *
 * All three are pure TypeScript with no engine dependency, so these run
 * against the page context without a WASM build — they only need the player
 * bundle, which exposes them through `player-test-hooks.ts`.
 */

test.beforeEach(async ({ page }) => {
  await page.goto('/rebirth_website/play');
  await page.waitForFunction(() => !!(window as any).mapMidiMessage, null, { timeout: 15_000 });
});

test.describe('WebMIDI mapping', () => {
  test('channels 1 and 2 address the two TB-303s, velocity sets accent', async ({ page }) => {
    const mapped = await page.evaluate(() => {
      const map = (window as any).mapMidiMessage;
      return {
        a: map(new Uint8Array([0x90, 60, 64])),
        b: map(new Uint8Array([0x91, 62, 120])),
        off: map(new Uint8Array([0x80, 60, 0])),
        zeroVelocityOff: map(new Uint8Array([0x90, 60, 0])),
      };
    });

    expect(mapped.a).toMatchObject({
      kind: 'acid-note',
      device: 'tb303-a',
      note: 60,
      accent: false,
      on: true,
    });
    expect(mapped.b).toMatchObject({
      kind: 'acid-note',
      device: 'tb303-b',
      note: 62,
      accent: true,
      on: true,
    });
    expect(mapped.off).toMatchObject({ kind: 'acid-note', on: false });
    expect(mapped.zeroVelocityOff).toMatchObject({ kind: 'acid-note', on: false });
  });

  test('channel 10 pads map to drum bits, CC 74/71 to cutoff and resonance', async ({ page }) => {
    const mapped = await page.evaluate(() => {
      const map = (window as any).mapMidiMessage;
      return {
        kick: map(new Uint8Array([0x99, 36, 100])),
        hat: map(new Uint8Array([0x99, 42, 40])),
        cutoff: map(new Uint8Array([0xb0, 74, 127])),
        resonance: map(new Uint8Array([0xb0, 71, 0])),
      };
    });

    expect(mapped.kick).toMatchObject({ kind: 'drum-hit', drumKey: 'bd', accent: true, on: true });
    expect(mapped.hat).toMatchObject({ kind: 'drum-hit', drumKey: 'ch', accent: false });
    // DeviceParamId 1 = Cutoff, 2 = Resonance (player-studio.ts / EngineCommands.h).
    expect(mapped.cutoff).toMatchObject({ kind: 'param', paramId: 1, value: 1 });
    expect(mapped.resonance).toMatchObject({ kind: 'param', paramId: 2, value: 0 });
  });

  test('clock, transport and unmapped CCs are ignored', async ({ page }) => {
    const ignored = await page.evaluate(() => {
      const map = (window as any).mapMidiMessage;
      return [
        map(new Uint8Array([0xf8])), // clock
        map(new Uint8Array([0xfa])), // start
        map(new Uint8Array([0xb0, 7, 90])), // CC 7 volume
        map(new Uint8Array([0xe0, 0, 64])), // pitch bend
        map(new Uint8Array([0x92, 60, 100])), // channel 3 — not mapped
      ];
    });
    expect(ignored).toEqual([null, null, null, null, null]);
  });
});

test.describe('shareable studio patches', () => {
  test('a patch round-trips through deflate + Base64URL', async ({ page }) => {
    const result = await page.evaluate(() => {
      const w = window as any;
      const patch = {
        version: 1,
        bpm: 138,
        steps: [
          {
            deviceIndex: 0,
            bank: 1,
            patternIndex: 2,
            stepIndex: 5,
            note: 51,
            drumExtra: 0,
            active: true,
            accent: true,
            slide: false,
          },
        ],
        params: [{ deviceIndex: 0, paramId: 1, value: 0.75 }],
      };
      const encoded = w.encodeStudioPatch(patch);
      return { encoded, decoded: w.decodeStudioPatch(encoded) };
    });

    // Base64URL only — nothing a query string would have to escape.
    expect(result.encoded).toMatch(/^[A-Za-z0-9_-]+$/);
    expect(result.decoded.bpm).toBe(138);
    expect(result.decoded.steps[0]).toMatchObject({
      deviceIndex: 0,
      bank: 1,
      patternIndex: 2,
      stepIndex: 5,
      note: 51,
      active: true,
      accent: true,
      slide: false,
    });
    expect(result.decoded.params[0]).toMatchObject({ deviceIndex: 0, paramId: 1, value: 0.75 });
  });

  test('?p= layers onto ?play= and keeps the base path', async ({ page }) => {
    const url = await page.evaluate(() => {
      const w = window as any;
      const patch = {
        version: 1,
        steps: [
          {
            deviceIndex: 2,
            bank: 0,
            patternIndex: 0,
            stepIndex: 0,
            note: 1,
            drumExtra: 0,
            active: true,
            accent: false,
            slide: false,
          },
        ],
        params: [],
      };
      return w.buildShareUrl(
        'https://ford442.github.io/rebirth_website/play?play=demo%2Fsong.rbs',
        patch
      );
    });

    expect(url.ok).toBe(true);
    const parsed = new URL(url.url);
    expect(parsed.pathname).toBe('/rebirth_website/play');
    expect(parsed.searchParams.get('play')).toBe('demo/song.rbs');
    expect(parsed.searchParams.get('p')).toBeTruthy();
  });

  test('an oversized patch is refused rather than truncated into a link', async ({ page }) => {
    const result = await page.evaluate(() => {
      const w = window as any;
      const steps = [];
      // Randomised notes so deflate cannot squeeze this under the cap.
      for (let i = 0; i < 4000; i++) {
        steps.push({
          deviceIndex: i % 4,
          bank: i % 4,
          patternIndex: i % 8,
          stepIndex: i % 16,
          note: Math.floor(Math.random() * 128),
          drumExtra: Math.floor(Math.random() * 8),
          active: true,
          accent: i % 3 === 0,
          slide: i % 5 === 0,
        });
      }
      return w.buildShareUrl('https://example.test/rebirth_website/play', {
        version: 1,
        steps,
        params: [],
      });
    });

    expect(result.ok).toBe(false);
    expect(result.reason).toBe('too-large');
  });

  test('a corrupt ?p= decodes to null instead of throwing', async ({ page }) => {
    const decoded = await page.evaluate(() =>
      (window as any).decodeStudioPatch('not-a-real-patch-!!')
    );
    expect(decoded).toBeNull();
  });
});

test.describe('catalog fetch classification', () => {
  test('404, CORP block and parse failure are told apart', async ({ page }) => {
    const reasons = await page.evaluate(() => {
      const w = window as any;
      return {
        notFound: w.classifyLoadError(new w.RbsFetchError(404, 'https://test.1ink.us/x.rbs'))
          .reason,
        httpError: w.classifyLoadError(new w.RbsFetchError(503, 'https://test.1ink.us/x.rbs'))
          .reason,
        parse: w.classifyLoadError(new w.RbsParseError('bad chunk')).reason,
        sameOrigin: w.classifyLoadError(new TypeError('Failed to fetch'), '/rebirth_website/a.rbs')
          .reason,
        crossOrigin: w.classifyLoadError(
          new TypeError('Failed to fetch'),
          'https://test.1ink.us/rb338/archive/rbs-songs/a.rbs'
        ).reason,
      };
    });

    expect(reasons.notFound).toBe('not-found');
    expect(reasons.httpError).toBe('http-error');
    expect(reasons.parse).toBe('parse');
    expect(reasons.sameOrigin).toBe('offline');
    // Off-origin only reads as a CORP block while the page is isolated;
    // otherwise it is an ordinary network failure.
    expect(['corp-blocked', 'offline']).toContain(reasons.crossOrigin);
  });
});
