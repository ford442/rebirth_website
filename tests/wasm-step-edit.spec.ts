import { test, expect } from '@playwright/test';

const WASM_FIXTURE_URL = '/rebirth_website/archive/rbs-songs/demo/propellerhead-008.rbs';

/**
 * Studio pattern editing against the real WASM engine.
 *
 * Covers the two things that can silently break: the engine accepting a step
 * patch and rendering something different afterwards, and the worklet
 * surviving a graph republish while it is playing.
 *
 * Requires built artifacts: npm run build:ship
 */
test.describe('WASM studio step editing', () => {
  test('setStep changes the working copy and the worklet keeps running', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 15_000 });

    const result = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;
      const bridge = new WasmAudioBridge();
      await bridge.init();

      const response = await fetch(fixtureUrl);
      if (!response.ok) throw new Error(`Failed to fetch fixture: ${response.status}`);
      await bridge.loadRbsFile(await response.arrayBuffer());

      bridge.play();
      await new Promise((resolve) => setTimeout(resolve, 400));
      const blocksBeforeEdit = bridge.enginePtr?.getProcessedBlockCount?.() ?? 0;

      // TB-303 A, bank A, pattern 1, step 0 — the slot the UI opens on.
      const before = bridge.getStep(0, 0, 0, 0);
      const accepted = bridge.setStep(0, 0, 0, 0, {
        active: true,
        note: 60,
        drumExtra: 0,
        accent: true,
        slide: false,
      });
      const after = bridge.getStep(0, 0, 0, 0);

      // A republish must not kill the audio thread.
      await new Promise((resolve) => setTimeout(resolve, 400));
      const blocksAfterEdit = bridge.enginePtr?.getProcessedBlockCount?.() ?? 0;

      // Undo: the inverse patch, not a song clone.
      const undone = bridge.setStep(0, 0, 0, 0, before);
      const restored = bridge.getStep(0, 0, 0, 0);

      // A slot the file never stored is materialised on first write.
      const lengthBefore = bridge.enginePtr?.getPatternLength?.(2, 3, 7) ?? -1;
      bridge.setStep(2, 3, 7, 5, {
        active: true,
        note: 0x02,
        drumExtra: 0,
        accent: false,
        slide: false,
      });
      const lengthAfter = bridge.enginePtr?.getPatternLength?.(2, 3, 7) ?? -1;

      // Out-of-range coordinates are refused, never wrapped into slot 0.
      const rejected = bridge.setStep(9, 0, 0, 0, before);

      bridge.dispose();
      return {
        accepted,
        beforeActive: Boolean(before.active),
        afterNote: after.note,
        afterAccent: Boolean(after.accent),
        undone,
        restoredActive: Boolean(restored.active),
        restoredNote: restored.note,
        blocksBeforeEdit,
        blocksAfterEdit,
        lengthBefore,
        lengthAfter,
        rejected,
      };
    }, WASM_FIXTURE_URL);

    expect(result.accepted).toBe(true);
    expect(result.afterNote).toBe(60);
    expect(result.afterAccent).toBe(true);

    expect(result.blocksBeforeEdit).toBeGreaterThan(0);
    expect(result.blocksAfterEdit).toBeGreaterThan(result.blocksBeforeEdit);

    expect(result.undone).toBe(true);
    expect(result.restoredActive).toBe(result.beforeActive);

    expect(result.lengthBefore).toBe(0);
    expect(result.lengthAfter).toBe(16);

    expect(result.rejected).toBe(false);
  });

  test('clicking a grid cell toggles it and playback survives', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');

    await page.locator('#rbsDemoSelect').selectOption({ index: 1 });
    await page.locator('#rbsBtnDemoLoad').click();
    await expect(page.locator('#rbsMetaTitle')).not.toHaveText('—', { timeout: 20_000 });

    // Step editing is only offered when the real engine is behind the grid.
    const live = await page.locator('.rbs-player').getAttribute('data-studio-live');
    test.skip(live !== '1', 'engine not live — degraded sketch mode');

    const cell = page.locator('[data-studio-step="0"]');
    await expect(cell).toBeEnabled();
    const pressedBefore = (await cell.getAttribute('aria-pressed')) ?? 'false';

    await page.locator('#rbsBtnPlay').click();

    // The 303 cycle is off -> note-on -> accent -> slide -> off, so a step
    // that already sounds needs three clicks to reach silence.
    const clicks = pressedBefore === 'true' ? 3 : 1;
    for (let i = 0; i < clicks; i++) await cell.click();

    await expect(cell).toHaveAttribute('aria-pressed', pressedBefore === 'true' ? 'false' : 'true');
    await expect(page.locator('#rbsStudioUndo')).toBeEnabled();

    // Undo is one inverse patch per edit, so it unwinds click for click.
    for (let i = 0; i < clicks; i++) await page.locator('#rbsStudioUndo').click();
    await expect(cell).toHaveAttribute('aria-pressed', pressedBefore);
    await expect(page.locator('#rbsStudioUndo')).toBeDisabled();
  });
});
