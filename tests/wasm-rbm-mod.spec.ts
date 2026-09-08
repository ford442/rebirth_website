import { test, expect } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const WASM_FIXTURE_URL = '/rebirth_website/archive/rbs-songs/demo/propellerhead-008.rbs';

/**
 * Browser-side `.rbm` mod loading.
 *
 * Requires the WASM artifacts to be built and present in public/wasm/:
 *   npm run wasm:build
 *
 * The mod fixture is read from disk here in Node and handed to the page as a
 * byte array, so nothing test-only has to be published under public/.
 */
const MOD_FIXTURE = Array.from(readFileSync(resolve('src/wasm/test-fixtures/mods/sample-kit.rbm')));

test.describe('WASM .rbm mod loading', () => {
  test.use({ serviceWorkers: 'allow' });

  test('loads a mod, reports its slots, and keeps the worklet running', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 5000 });

    const result = await page.evaluate(
      async ({ modBytes, fixtureUrl }) => {
        const WasmAudioBridge = (window as any).WasmAudioBridge;
        const bridge = new WasmAudioBridge();
        await bridge.init();

        const audioContext = bridge.ctx;
        if (!audioContext) throw new Error('Audio context not exposed');
        await audioContext.suspend();

        const songBuffer = await (await fetch(fixtureUrl)).arrayBuffer();
        await bridge.loadRbsFile(songBuffer);

        const before = bridge.hasMod();

        const modBuffer = new Uint8Array(modBytes).buffer;
        const mod = await bridge.loadRbmFile(modBuffer);

        // Render through the mod to prove the audio thread survives a graph
        // swap — a malloc inside the callback would hang or crash here.
        bridge.setTempoMultiplier(4);
        bridge.play();
        let peak = 0;
        for (let block = 0; block < 64; block += 1) {
          peak = Math.max(peak, bridge.enginePtr.renderTestBlock(128));
        }
        const blocksAfterPlay = bridge.enginePtr.getProcessedBlockCount();

        const after = bridge.hasMod();
        bridge.clearMod();
        const cleared = bridge.hasMod();

        bridge.stop();
        bridge.dispose();

        return {
          before,
          after,
          cleared,
          peak,
          blocksAfterPlay,
          title: mod.title,
          status: mod.status,
          loadedSlots: mod.loadedSlots,
          skinCount: mod.skinCount,
          slots: mod.resources.filter((r: any) => r.loaded).map((r: any) => r.slot),
          // Proof that payload bytes never crossed into JS.
          hasByteField: Object.prototype.hasOwnProperty.call(mod.resources[0], 'bytes'),
        };
      },
      { modBytes: MOD_FIXTURE, fixtureUrl: WASM_FIXTURE_URL }
    );

    expect(result.before).toBe(false);
    expect(result.after).toBe(true);
    expect(result.cleared).toBe(false);

    expect(result.title).toBe('Sample Kit Test Mod');
    expect(result.status).toBe('ok');
    expect(result.loadedSlots).toBe(3);
    expect(result.skinCount).toBe(1);
    expect(result.slots.sort()).toEqual(['tb303-saw', 'tr808-kick', 'tr909-snare']);

    // The skin never becomes audio, and no payload bytes reach JS.
    expect(result.hasByteField).toBe(false);

    expect(result.peak).toBeGreaterThan(0.0001);
    expect(result.blocksAfterPlay).toBeGreaterThan(0);
  });

  test('rejects a mod with no playable samples without breaking playback', async ({ page }) => {
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

        // Random bytes: parses as neither a container nor audio.
        const junk = new Uint8Array(512).fill(0x7f).buffer;
        let threw = false;
        let message = '';
        try {
          await bridge.loadRbmFile(junk);
        } catch (err: any) {
          threw = true;
          message = String(err?.message ?? '');
        }

        // Playback must still work after a rejected mod.
        bridge.setTempoMultiplier(4);
        bridge.play();
        let peak = 0;
        for (let block = 0; block < 64; block += 1) {
          peak = Math.max(peak, bridge.enginePtr.renderTestBlock(128));
        }
        bridge.stop();
        bridge.dispose();

        return { threw, message, hasMod: false, peak };
      },
      { fixtureUrl: WASM_FIXTURE_URL }
    );

    expect(result.threw).toBe(true);
    expect(result.message.length).toBeGreaterThan(0);
    expect(result.peak).toBeGreaterThan(0.0001);
  });

  test('mod controls are present and wired in the player UI', async ({ page }) => {
    await page.goto('/rebirth_website/');

    const loadButton = page.locator('#rbsBtnModLoad');
    await expect(loadButton).toBeVisible();
    await expect(page.locator('#rbsModStatus')).toBeVisible();

    // The Clear button only appears once a mod is actually loaded.
    await expect(page.locator('#rbsBtnModClear')).toBeHidden();

    const modInput = page.locator('#rbsModFileInput');
    await expect(modInput).toHaveAttribute('accept', '.rbm');
  });
});
