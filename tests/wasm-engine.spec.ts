import { test, expect } from '@playwright/test';

const WASM_FIXTURE_URL = '/rebirth_website/archive/rbs-songs/demo/propellerhead-008.rbs';

/**
 * Integration test for the WASM audio engine.
 *
 * Requires the WASM artifacts to be built and present in public/wasm/:
 *   npm run wasm:build
 */
test.describe('WASM audio engine', () => {
  test.use({ serviceWorkers: 'allow' });
  test('AudioContext uses interactive latency and reports diagnostics', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const audioInfo = await page.evaluate(async () => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();
      const ctx = bridge.ctx;
      const diagnostics = bridge.audioDiagnostics;
      bridge.dispose();
      return {
        latencyHint: ctx?.latencyHint ?? null,
        sampleRate: ctx?.sampleRate ?? null,
        diagnosticsSampleRate: diagnostics?.sampleRate ?? null,
        diagnosticsHint: diagnostics?.latencyHint ?? null,
        requestedSampleRate: diagnostics?.requestedSampleRate ?? null,
      };
    });

    expect(audioInfo.diagnosticsHint).toBe('interactive');
    if (audioInfo.latencyHint != null) {
      expect(audioInfo.latencyHint).toBe('interactive');
    }
    expect(audioInfo.sampleRate).toBeGreaterThan(0);
    expect(audioInfo.diagnosticsSampleRate).toBe(audioInfo.sampleRate);
    expect(
      audioInfo.requestedSampleRate === 44100 || audioInfo.requestedSampleRate === null
    ).toBe(true);
  });

  test('retries AudioContext without sampleRate when preferred rate is rejected', async ({
    page,
  }) => {
    await page.addInitScript(() => {
      const NativeAudioContext = window.AudioContext;
      (window as any).__audioContextAttempts = 0;
      const MockAudioContext = function (
        this: AudioContext,
        options?: AudioContextOptions
      ): AudioContext {
        (window as any).__audioContextAttempts += 1;
        const attempts = (window as any).__audioContextAttempts as number;
        if (attempts === 1 && options?.sampleRate != null) {
          throw new DOMException('sampleRate not supported', 'NotSupportedError');
        }
        return new NativeAudioContext(
          attempts === 1 ? options : { latencyHint: options?.latencyHint }
        );
      } as unknown as typeof AudioContext;
      MockAudioContext.prototype = NativeAudioContext.prototype;
      (window as any).AudioContext = MockAudioContext;
    });

    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).createProductionAudioContext, null, {
      timeout: 5000,
    });

    const audioInfo = await page.evaluate(() => {
      (window as any).__audioContextAttempts = 0;
      const createProductionAudioContext = (window as any).createProductionAudioContext;
      const { context, diagnostics } = createProductionAudioContext(44100, 'interactive');
      return {
        sampleRate: context.sampleRate,
        diagnosticsSampleRate: diagnostics.sampleRate,
        requestedSampleRate: diagnostics.requestedSampleRate,
      };
    });

    expect(audioInfo.requestedSampleRate).toBeNull();
    expect(audioInfo.sampleRate).toBeGreaterThan(0);
    expect(audioInfo.diagnosticsSampleRate).toBe(audioInfo.sampleRate);
  });

  test('initialises, loads a fixture, and advances playback position', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');

    // Wait for the dev-mode global export from RbsPlayer.astro.
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const playback = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      const response = await fetch(fixtureUrl);
      if (!response.ok) {
        throw new Error(`Failed to fetch fixture: ${response.status}`);
      }
      const buffer = await response.arrayBuffer();
      await bridge.loadRbsFile(buffer);
      await bridge.ctx?.resume();
      const deadline = Date.now() + 5000;
      while (bridge.enginePtr.getProcessedBlockCount() === 0 && Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
      const workletBlocks = bridge.enginePtr.getProcessedBlockCount();
      await bridge.ctx?.suspend();

      if (typeof bridge.setDeviceParam !== 'function' || !bridge.setDeviceParam(0, 1, 0.25)) {
        throw new Error('setDeviceParam cutoff failed');
      }
      bridge.setDeviceParam(0, 2, 0.8);
      bridge.setDeviceParam(0, 4, 0.4);

      bridge.setTempoMultiplier(4);
      bridge.play();
      for (let block = 0; block < 64; block += 1) {
        bridge.enginePtr.renderTestBlock(128);
      }

      const pos = bridge.enginePtr.getPlaybackPosition();
      const playing = bridge.enginePtr.isPlaying();
      const contextState = bridge.ctx?.state;
      bridge.stop();
      bridge.dispose();
      return { pos, workletBlocks, playing, contextState };
    }, WASM_FIXTURE_URL);

    expect(playback.workletBlocks).toBeGreaterThan(0);
    expect(playback.playing).toBe(true);
    expect(playback.contextState).toBe('suspended');
    expect(playback.pos).toBeDefined();
    expect(playback.pos.bar).toBeGreaterThanOrEqual(1);
    // Position should have moved beyond the initial step after ~400ms of playback.
    expect(playback.pos.step + playback.pos.bar).toBeGreaterThan(1);
  });

  test('produces non-silent output while playing', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, {
      timeout: 5000,
    });

    const hasSignal = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      const audioContext = bridge.ctx;
      if (!audioContext) throw new Error('Audio context not exposed');
      await audioContext.suspend();

      const response = await fetch(fixtureUrl);
      const buffer = await response.arrayBuffer();
      await bridge.loadRbsFile(buffer);
      bridge.setTempoMultiplier(4);
      bridge.play();
      let peak = 0;
      for (let block = 0; block < 64; block += 1) {
        peak = Math.max(peak, bridge.enginePtr.renderTestBlock(128));
      }
      bridge.stop();
      bridge.dispose();
      return peak > 0.0001;
    }, WASM_FIXTURE_URL);

    expect(hasSignal).toBe(true);
  });
});
