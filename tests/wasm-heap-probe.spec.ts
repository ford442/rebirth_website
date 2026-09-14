import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import {
  fixturePayloads,
  runHeapProbeInPage,
  slackLimit,
} from '../scripts/wasm-heap-probe.mjs';

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
});
