import { test, expect } from '@playwright/test';

const WASM_FIXTURE_URL = '/rebirth_website/archive/rbs-songs/demo/propellerhead-008.rbs';

/**
 * Offline bounce, stems and MIDI export in the browser.
 *
 * Requires built WASM artifacts in public/wasm/ (`npm run wasm:build`).
 */
test.describe('Studio export', () => {
  test.use({ serviceWorkers: 'allow' });

  test('bounces a valid, reproducible WAV that decodes to real audio', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 5000 });

    const result = await page.evaluate(
      async ({ fixtureUrl }) => {
        const WasmAudioBridge = (window as any).WasmAudioBridge;
        const bridge = new WasmAudioBridge();
        await bridge.init();

        const audioContext = bridge.ctx;
        if (!audioContext) throw new Error('Audio context not exposed');
        await audioContext.suspend();

        const songBuffer = await (await fetch(fixtureUrl)).arrayBuffer();
        await bridge.loadRbsFile(songBuffer);

        // Two seconds is plenty to prove the path without a slow full bounce.
        const frames = 2 * Math.round(audioContext.sampleRate);
        const first = bridge.bounceToWav(frames);
        const second = bridge.bounceToWav(frames);

        // Decode the produced file with the browser's own decoder: if this
        // succeeds, the header we hand-wrote is one real software accepts.
        const decoded = await audioContext.decodeAudioData(first.bytes.buffer.slice(0));
        const channel = decoded.getChannelData(0);
        let sumSq = 0;
        for (let i = 0; i < channel.length; i += 1) sumSq += channel[i] * channel[i];
        const rms = Math.sqrt(sumSq / channel.length);

        // Byte-for-byte equality across two bounces of the same song.
        let identical = first.bytes.byteLength === second.bytes.byteLength;
        if (identical) {
          for (let i = 0; i < first.bytes.byteLength; i += 1) {
            if (first.bytes[i] !== second.bytes[i]) {
              identical = false;
              break;
            }
          }
        }

        bridge.dispose();
        return {
          filename: first.filename,
          mimeType: first.mimeType,
          byteLength: first.bytes.byteLength,
          riff: String.fromCharCode(...first.bytes.slice(0, 4)),
          wave: String.fromCharCode(...first.bytes.slice(8, 12)),
          identical,
          decodedChannels: decoded.numberOfChannels,
          decodedDuration: decoded.duration,
          decodedSampleRate: decoded.sampleRate,
          rms,
        };
      },
      { fixtureUrl: WASM_FIXTURE_URL }
    );

    expect(result.riff).toBe('RIFF');
    expect(result.wave).toBe('WAVE');
    expect(result.mimeType).toBe('audio/wav');
    expect(result.filename).toMatch(/\.wav$/);

    // A 16-bit stereo file is 4 bytes per frame, plus the 44-byte header.
    expect(result.byteLength).toBe(44 + 2 * result.decodedSampleRate * 4);

    expect(result.decodedChannels).toBe(2);
    expect(result.decodedDuration).toBeCloseTo(2, 1);
    expect(result.rms).toBeGreaterThan(0.001); // not a silent file

    // The whole point of an offline engine: the same song bounces the same way.
    expect(result.identical).toBe(true);
  });

  test('stems render one file per device, each different from the master', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 5000 });

    const result = await page.evaluate(
      async ({ fixtureUrl }) => {
        const WasmAudioBridge = (window as any).WasmAudioBridge;
        const bridge = new WasmAudioBridge();
        await bridge.init();
        await bridge.ctx.suspend();

        const songBuffer = await (await fetch(fixtureUrl)).arrayBuffer();
        await bridge.loadRbsFile(songBuffer);

        const frames = Math.round(bridge.ctx.sampleRate);
        const master = bridge.bounceToWav(frames);
        const stems = bridge.bounceStems(frames);

        const sameAsMaster = stems.filter((stem: any) => {
          if (stem.bytes.byteLength !== master.bytes.byteLength) return false;
          for (let i = 0; i < stem.bytes.byteLength; i += 1) {
            if (stem.bytes[i] !== master.bytes[i]) return false;
          }
          return true;
        }).length;

        bridge.dispose();
        return {
          count: stems.length,
          names: stems.map((s: any) => s.filename),
          allWav: stems.every((s: any) => s.mimeType === 'audio/wav'),
          sameSize: stems.every((s: any) => s.bytes.byteLength === master.bytes.byteLength),
          sameAsMaster,
        };
      },
      { fixtureUrl: WASM_FIXTURE_URL }
    );

    expect(result.count).toBe(4);
    expect(result.allWav).toBe(true);
    expect(result.sameSize).toBe(true);
    expect(result.sameAsMaster).toBe(0);
    expect(result.names.join(' ')).toMatch(/303a/);
    expect(result.names.join(' ')).toMatch(/909/);
  });

  test('exports a Standard MIDI File a decoder can walk', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 5000 });

    const result = await page.evaluate(
      async ({ fixtureUrl }) => {
        const WasmAudioBridge = (window as any).WasmAudioBridge;
        const songToMidi = (window as any).songToMidi;
        const bridge = new WasmAudioBridge();
        await bridge.init();
        await bridge.ctx.suspend();

        const songBuffer = await (await fetch(fixtureUrl)).arrayBuffer();
        const song = await bridge.loadRbsFile(songBuffer);
        const bytes: Uint8Array = songToMidi(song, { bars: 2 });
        bridge.dispose();

        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const id = (offset: number) =>
          String.fromCharCode(
            bytes[offset],
            bytes[offset + 1],
            bytes[offset + 2],
            bytes[offset + 3]
          );

        // Walk every chunk; landing exactly on the end proves the length fields.
        let offset = 0;
        const chunks: string[] = [];
        while (offset < bytes.byteLength) {
          chunks.push(id(offset));
          offset += 8 + view.getUint32(offset + 4);
        }

        return {
          header: id(0),
          format: view.getUint16(8),
          trackCount: view.getUint16(10),
          division: view.getUint16(12),
          chunks,
          walkedTo: offset,
          byteLength: bytes.byteLength,
        };
      },
      { fixtureUrl: WASM_FIXTURE_URL }
    );

    expect(result.header).toBe('MThd');
    expect(result.format).toBe(1); // Type 1: one track per device
    expect(result.division).toBe(96);
    expect(result.trackCount).toBeGreaterThan(1);
    expect(result.chunks[0]).toBe('MThd');
    expect(result.chunks.slice(1).every((c) => c === 'MTrk')).toBe(true);
    // Chunk lengths are self-consistent: the walk ends exactly at EOF.
    expect(result.walkedTo).toBe(result.byteLength);
  });

  // The issue's stated success path: open a song, shorten the 303 decay,
  // download a WAV that matches what you are hearing. Live knob moves are
  // session-only in the engine, so the bounce has to replay them explicitly
  // or it would silently render the song's untouched values.
  test('a live knob tweak changes the bounced audio', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 5000 });

    const result = await page.evaluate(
      async ({ fixtureUrl }) => {
        const WasmAudioBridge = (window as any).WasmAudioBridge;
        const bridge = new WasmAudioBridge();
        await bridge.init();
        await bridge.ctx.suspend();

        const songBuffer = await (await fetch(fixtureUrl)).arrayBuffer();
        await bridge.loadRbsFile(songBuffer);

        const frames = Math.round(bridge.ctx.sampleRate);
        const before = bridge.bounceToWav(frames);

        // DeviceParamId::Decay == 4, on TB-303 B (device 1) — the audible 303
        // in this fixture. Shorten it all the way.
        bridge.setDeviceParam(1, 4, 0.0);
        const after = bridge.bounceToWav(frames);

        // …and putting it back must restore the original bounce exactly.
        bridge.setDeviceParam(1, 4, 0.5);
        const restored = bridge.bounceToWav(frames);

        const same = (a: Uint8Array, b: Uint8Array) => {
          if (a.byteLength !== b.byteLength) return false;
          for (let i = 0; i < a.byteLength; i += 1) if (a[i] !== b[i]) return false;
          return true;
        };

        bridge.dispose();
        return {
          changed: !same(before.bytes, after.bytes),
          sameSize: before.bytes.byteLength === after.bytes.byteLength,
          restoredDiffersFromShortened: !same(after.bytes, restored.bytes),
        };
      },
      { fixtureUrl: WASM_FIXTURE_URL }
    );

    expect(result.sameSize).toBe(true);
    expect(result.changed).toBe(true);
    expect(result.restoredDiffersFromShortened).toBe(true);
  });

  test('export controls are present and gated on a loaded song', async ({ page }) => {
    await page.goto('/rebirth_website/');

    await expect(page.locator('#rbsBtnBounce')).toBeVisible();
    await expect(page.locator('#rbsBtnStems')).toBeVisible();
    await expect(page.locator('#rbsBtnMidi')).toBeVisible();

    // Nothing loaded yet, so nothing is exportable.
    await expect(page.locator('#rbsBtnBounce')).toBeDisabled();
    await expect(page.locator('#rbsBtnMidi')).toBeDisabled();
  });
});
