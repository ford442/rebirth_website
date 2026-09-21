import { test, expect } from '@playwright/test';

const WASM_FIXTURE_URL = '/rebirth_website/archive/rbs-songs/demo/propellerhead-008.rbs';

/**
 * `.rbs` write-back in the browser.
 *
 * The visitor-facing promise: edit a couple of steps in the studio grid,
 * hit SAVE .RBS, and the downloaded file opens in this player with those
 * edits intact. The bytes come from C++ (RbsWriter), so this spec is about
 * the round trip through the Embind boundary and back through loadRbsFile,
 * not about the container layout — tests/test_writer.cpp covers that.
 *
 * Requires built artifacts: npm run build:ship
 */
test.describe('Studio .rbs save', () => {
  test('saved edits reload into the player and still play', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');
    await page.waitForFunction(() => !!(window as any).WasmAudioBridge, null, { timeout: 15_000 });

    const result = await page.evaluate(async (fixtureUrl) => {
      const WasmAudioBridge = (window as any).WasmAudioBridge;

      const bridge = new WasmAudioBridge();
      await bridge.init();
      await bridge.ctx?.suspend();
      const original = await bridge.loadRbsFile(await (await fetch(fixtureUrl)).arrayBuffer());

      // Two edits, on two different devices, so the save has to carry both
      // the 303 note/flag encoding and the drum bitfield.
      bridge.setStep(0, 0, 0, 3, {
        active: true,
        note: 60,
        drumExtra: 0,
        accent: true,
        slide: true,
      });
      bridge.setStep(2, 0, 0, 7, {
        active: true,
        note: 0x03,
        drumExtra: 0x04,
        accent: false,
        slide: false,
      });

      const file = bridge.saveRbs();
      // Copy out before disposing: the array is JS-owned, the engine is not.
      const savedBytes = file.bytes.slice(0);
      bridge.dispose();

      // A second bridge, as if the file had just been dropped on the page.
      const reloaded = new WasmAudioBridge();
      await reloaded.init();
      await reloaded.ctx?.suspend();
      const song = await reloaded.loadRbsFile(savedBytes.buffer.slice(0));

      const step303 = reloaded.getStep(0, 0, 0, 3);
      const step808 = reloaded.getStep(2, 0, 0, 7);

      reloaded.play();
      await new Promise((resolve) => setTimeout(resolve, 400));
      const blocks = reloaded.enginePtr?.getProcessedBlockCount?.() ?? 0;
      reloaded.stop();
      reloaded.dispose();

      return {
        filename: file.filename,
        mimeType: file.mimeType,
        byteLength: savedBytes.byteLength,
        magic: String.fromCharCode(...savedBytes.slice(0, 4)),
        marker: String.fromCharCode(...savedBytes.slice(8, 12)),
        originalTitle: original.title,
        reloadedTitle: song.title,
        originalTempo: original.bpm,
        reloadedTempo: song.bpm,
        step303,
        step808,
        blocks,
      };
    }, WASM_FIXTURE_URL);

    expect(result.filename).toMatch(/-exported\.rbs$/);
    expect(result.mimeType).toBe('application/octet-stream');
    expect(result.byteLength).toBeGreaterThan(1024);

    // Always a ReBirth 2.x container, whatever the source file was.
    expect(result.magic).toBe('CAT ');
    expect(result.marker).toBe('RB40');

    expect(result.reloadedTitle).toBe(result.originalTitle);
    expect(result.reloadedTempo).toBeCloseTo(result.originalTempo, 2);

    expect(result.step303).toMatchObject({ active: true, note: 60, accent: true, slide: true });
    expect(result.step808).toMatchObject({ active: true, note: 0x03, drumExtra: 0x04 });

    // The reloaded file is a real, playable song, not just parseable bytes.
    expect(result.blocks).toBeGreaterThan(0);
  });
});
