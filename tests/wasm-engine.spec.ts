import { test, expect } from '@playwright/test';

const WASM_FIXTURE_URL = '/rb338/wasm/test-fixtures/standard-rebirth.rbs';

/**
 * Integration test for the WASM audio engine.
 *
 * Requires the WASM artifacts to be built and present in public/wasm/:
 *   npm run wasm:build
 */
test.describe('WASM audio engine', () => {
  test('initialises, loads a fixture, and advances playback position', async ({ page }) => {
    await page.goto('/rb338');
    await page.waitForSelector('.rbs-player');

    // Wait for the dev-mode global export from RbsPlayer.astro.
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const position = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      const response = await fetch(fixtureUrl);
      if (!response.ok) {
        throw new Error(`Failed to fetch fixture: ${response.status}`);
      }
      const buffer = await response.arrayBuffer();
      await bridge.loadRbsFile(buffer);
      bridge.play();

      // Let the audio thread render a few 128-frame quanta.
      await new Promise((resolve) => setTimeout(resolve, 400));

      const pos = bridge.enginePtr.getPlaybackPosition();
      bridge.stop();
      bridge.dispose();
      return pos;
    }, WASM_FIXTURE_URL);

    expect(position).toBeDefined();
    expect(position.bar).toBeGreaterThanOrEqual(1);
    // Position should have moved beyond the initial step after ~400ms of playback.
    expect(position.step + position.bar).toBeGreaterThan(1);
  });

  test('produces non-silent output while playing', async ({ page }) => {
    await page.goto('/rb338');
    await page.waitForSelector('.rbs-player');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const hasSignal = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      // Create an analyser after the bridge's gain node to inspect output.
      const audioContext = bridge.ctx;
      const gainNode = bridge.masterGain;
      if (!audioContext || !gainNode) {
        throw new Error('Audio graph nodes not exposed');
      }

      const analyser = audioContext.createAnalyser();
      analyser.fftSize = 256;
      gainNode.disconnect();
      gainNode.connect(analyser);
      analyser.connect(audioContext.destination);

      const response = await fetch(fixtureUrl);
      const buffer = await response.arrayBuffer();
      await bridge.loadRbsFile(buffer);
      bridge.play();
      await new Promise((resolve) => setTimeout(resolve, 500));

      const data = new Float32Array(analyser.frequencyBinCount);
      analyser.getFloatFrequencyData(data);

      // Check that some frequency bin has measurable energy.
      const maxDb = Math.max(...data);
      bridge.stop();
      bridge.dispose();
      return maxDb > -100;
    }, WASM_FIXTURE_URL);

    expect(hasSignal).toBe(true);
  });
});
