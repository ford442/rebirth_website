import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/**
 * TRAK automation must survive the browser load path (parse + load in C++).
 *
 * Requires built WASM artifacts (`npm run wasm:build`) and WASM_BUILT=1 in CI.
 * Fixture bytes are injected from disk so nothing extra is published under public/.
 */
const SONG_FIXTURE = Array.from(
  readFileSync(resolve('src/wasm/test-fixtures/standard-rebirth.rbs'))
);

test.describe('TRAK automation through WasmAudioBridge', () => {
  test.use({ serviceWorkers: 'allow' });

  test('bounce energy differs across bars of an automated song', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const result = await page.evaluate(async ({ songBytes }) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      const audioContext = bridge.ctx;
      if (!audioContext) throw new Error('Audio context not exposed');
      await audioContext.suspend();

      const buffer = new Uint8Array(songBytes).buffer;
      await bridge.loadRbsFile(buffer);

      const sampleRate = audioContext.sampleRate;
      const bpm = bridge.enginePtr.getTempo();
      const framesPerBar = Math.round(sampleRate * (60 / bpm) * 4);
      const frames = framesPerBar * 10;
      const bounced = await bridge.bounceToWav(frames);
      if (!bounced) throw new Error('bounceToWav returned null');

      const decoded = await audioContext.decodeAudioData(bounced.bytes.buffer.slice(0));
      const left = decoded.getChannelData(0);

      const rmsOf = (start: number, count: number) => {
        let sumSq = 0;
        const end = Math.min(start + count, left.length);
        for (let i = start; i < end; i += 1) sumSq += left[i] * left[i];
        return Math.sqrt(sumSq / Math.max(1, end - start));
      };

      const early = rmsOf(0, framesPerBar);
      const late = rmsOf(framesPerBar * 8, framesPerBar);

      bridge.dispose();
      return {
        bpm,
        early,
        late,
        ratio: late === 0 ? (early === 0 ? 1 : Infinity) : early / late,
        frames,
        decodedFrames: left.length,
      };
    }, { songBytes: SONG_FIXTURE });

    expect(result.decodedFrames).toBeGreaterThan(0);
    expect(result.early).toBeGreaterThan(0);
    expect(result.late).toBeGreaterThan(0);
    // Mixer/filter TRAK moves in standard-rebirth; static knobs would keep
    // early and late RMS within a few percent.
    const relative = Math.abs(result.early - result.late) / Math.max(result.early, result.late);
    expect(relative).toBeGreaterThan(0.03);
  });
});
