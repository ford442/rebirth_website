import { test, expect } from '@playwright/test';

const WASM_FIXTURE_URL = '/rebirth_website/archive/rbs-songs/demo/propellerhead-008.rbs';

/**
 * WASM engine integration against the production preview server (no dev COI headers).
 * Requires built artifacts: npm run build:ship
 */
test.describe('WASM audio engine (preview / GitHub Pages parity)', () => {
  test('crossOriginIsolated and engine reaches ready', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');

    const isolation = await page.evaluate(async () => {
      const deadline = Date.now() + 15000;
      while (Date.now() < deadline) {
        if (window.crossOriginIsolated) {
          return true;
        }
        await new Promise((resolve) => setTimeout(resolve, 200));
      }
      return window.crossOriginIsolated;
    });

    expect(isolation).toBe(true);

    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const result = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      const response = await fetch(fixtureUrl);
      if (!response.ok) {
        throw new Error(`Failed to fetch fixture: ${response.status}`);
      }
      const buffer = await response.arrayBuffer();
      await bridge.loadRbsFile(buffer);

      const status = bridge.playerStatus;
      const workletBlocks = bridge.enginePtr?.getProcessedBlockCount?.() ?? 0;

      bridge.dispose();
      return { status, workletBlocks, crossOriginIsolated: window.crossOriginIsolated };
    }, WASM_FIXTURE_URL);

    expect(result.crossOriginIsolated).toBe(true);
    expect(result.status).toBe('ready');
    expect(result.workletBlocks).toBeGreaterThan(0);
  });
});
