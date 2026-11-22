import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fixturePayloads, runHeapProbeInPage, slackLimit } from '../scripts/wasm-heap-probe.mjs';

const MANIFEST = path.join(process.cwd(), 'public/wasm/wasm-build.json');

test.describe('WASM heap probe', () => {
  test.use({ serviceWorkers: 'allow' });

  test('live+mod+worker bounce stays under INITIAL_MEMORY minus 8 MiB', async ({ page }) => {
    test.setTimeout(180_000);
    const manifest = JSON.parse(readFileSync(MANIFEST, 'utf8')) as {
      build: { initialMemory: number };
    };
    const initialMemory = manifest.build.initialMemory;
    expect(initialMemory).toBeGreaterThan(0);

    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 15000 });

    const result = await runHeapProbeInPage(page, fixturePayloads());
    const livePeak = Math.max(
      result.peaks.afterInit.usedBytes,
      result.peaks.afterSong.usedBytes,
      result.peaks.afterSampleKit.usedBytes,
      result.peaks.afterLargeKit.usedBytes,
      result.peaks.afterLegacyCleanup.usedBytes,
      result.peaks.liveDuringWorkerBounce.usedBytes
    );

    expect(result.bounceWavBytes).toBeGreaterThan(44);
    expect(livePeak).toBeLessThanOrEqual(slackLimit(initialMemory));
  });

  // The whole-file bounce reserved ~10.6 MiB per minute in the worker's fixed
  // 64 MiB heap and aborted past four or five minutes. blue-planet is 530
  // bars at 140 BPM — about 15 minutes, ~153 MiB of WAV — so this only
  // passes because the worker streams fixed-size chunks out of its heap.
  test('a full-length 15-minute bounce and its stems fit the worker heap', async ({ page }) => {
    test.setTimeout(600_000);
    const manifest = JSON.parse(readFileSync(MANIFEST, 'utf8')) as {
      build: { initialMemory: number };
    };
    const initialMemory = manifest.build.initialMemory;
    const songB64 = readFileSync(
      path.join(process.cwd(), 'src/wasm/test-fixtures/blue-planet.rbs')
    ).toString('base64');

    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 15000 });

    const result = await page.evaluate(async (b64) => {
      const bin = atob(b64);
      const buf = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i += 1) buf[i] = bin.charCodeAt(i);
      const bridge = new (window as any).WasmAudioBridge();
      await bridge.init();
      await bridge.loadRbsFile(buf.buffer);
      const fullFrames = bridge.songLengthFrames();
      // Keep only sizes: four 150 MiB stems are a lot of renderer memory.
      const master = await bridge.bounceToWav();
      const masterBytes = master.bytes.byteLength;
      const masterRiff = String.fromCharCode(...master.bytes.slice(0, 4));
      const masterHeap = bridge.lastBounceHeap;
      const stems = await bridge.bounceStems();
      const stemBytes = stems.map((s: any) => s.bytes.byteLength);
      const stemHeap = bridge.lastBounceHeap;
      bridge.dispose();
      return { fullFrames, masterBytes, masterRiff, masterHeap, stemBytes, stemHeap };
    }, songB64);

    // 15 minutes at 44.1 kHz, rendered in full.
    expect(result.fullFrames).toBeGreaterThan(44100 * 60 * 14);
    expect(result.masterRiff).toBe('RIFF');
    expect(result.masterBytes).toBe(44 + result.fullFrames * 4);
    expect(result.stemBytes).toEqual([1, 2, 3, 4].map(() => result.masterBytes));
    for (const heap of [result.masterHeap, result.stemHeap]) {
      expect(heap).not.toBeNull();
      expect(heap!.heapSize).toBe(initialMemory); // fixed heap: no growth
      expect(heap!.peakBytes).toBeLessThanOrEqual(slackLimit(initialMemory));
    }
  });
});
