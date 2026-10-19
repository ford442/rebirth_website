import { test, expect, type Page } from '@playwright/test';

/**
 * Player behaviours that only a real browser can vouch for: the production
 * bundle's `window` surface, a real 404, and a real drop event.
 *
 * Everything that is pure TypeScript — the patch codec, the SMF writer, the
 * MIDI map, error classification, the bridge's init lifecycle — is covered by
 * Vitest (`npm run test:unit`). These specs run without a WASM build (the
 * player ends up in degraded mode); the orchestration around a *working*
 * bridge is exercised in `src/wasm/js/player-ui.test.ts`.
 */

const LOCAL_DEMO_URL = '/rebirth_website/archive/rbs-songs/demo/noah-cohn-20.rbs';

const HOOKS = [
  'WasmAudioBridge',
  'DegradedRbsPlayer',
  'createProductionAudioContext',
  'attachAudioContextLifecycle',
  'resolveAudioContextConstructor',
  'songToMidi',
];

/** Wait until the player has finished its first init attempt, one way or the other. */
async function playerReady(page: Page) {
  await page.waitForSelector('.rbs-player[data-player-mode]', { timeout: 30_000 });
}

/** Hand the player a file the way a drag-and-drop does. */
async function dropFile(page: Page, name: string, url = LOCAL_DEMO_URL) {
  await page.evaluate(
    async ({ name, url }) => {
      const res = await fetch(url);
      const bytes = await res.arrayBuffer();
      const file = new File([bytes], name, { type: 'application/octet-stream' });
      const dataTransfer = new DataTransfer();
      dataTransfer.items.add(file);
      const event = new DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer });
      document.getElementById('rbsDropZone')!.dispatchEvent(event);
    },
    { name, url }
  );
}

test.describe('window test hooks: the Playwright config opts every spec in', () => {
  // The wasm-*.spec.ts files drive the engine through these and never ask for
  // them themselves; this is the one place that proves the config does.
  test('a spec with no setup of its own still gets them', async ({ page }) => {
    await page.goto('/rebirth_website/play');
    await playerReady(page);
    const exposed = await page.evaluate((names) => names.filter((n) => n in window), HOOKS);
    expect(exposed).toEqual(HOOKS);
  });
});

test.describe('window test hooks are opt-in', () => {
  // The Playwright config switches the hooks on for every other spec through
  // localStorage; here we are a visitor who did not.
  test.use({ storageState: { cookies: [], origins: [] } });

  const exposed = (page: Page) =>
    page.evaluate((names) => names.filter((name) => name in window), HOOKS);

  test('an ordinary visit to the production site exposes no engine hooks', async ({ page }) => {
    // `astro dev` always exposes them; only the production preview is the real thing.
    test.skip(!process.env.CI, 'CI serves the production preview; locally this is the dev server');

    await page.goto('/rebirth_website/play');
    await playerReady(page);
    expect(await exposed(page)).toEqual([]);
  });

  test('?test asks for them', async ({ page }) => {
    await page.goto('/rebirth_website/play?test');
    await playerReady(page);
    expect(await exposed(page)).toEqual(HOOKS);
  });

  test('the localStorage flag asks for them', async ({ page }) => {
    await page.addInitScript(() => localStorage.setItem('rb-test-hooks', '1'));
    await page.goto('/rebirth_website/play');
    await playerReady(page);
    expect(await exposed(page)).toEqual(HOOKS);
  });

  test('pure helpers are never on window', async ({ page }) => {
    await page.goto('/rebirth_website/play?test');
    await playerReady(page);
    const leaked = await page.evaluate(() =>
      [
        'mapMidiMessage',
        'encodeStudioPatch',
        'decodeStudioPatch',
        'buildShareUrl',
        'classifyLoadError',
        'RbsFetchError',
        'RbsParseError',
      ].filter((name) => name in window)
    );
    expect(leaked).toEqual([]);
  });
});

test.describe('loading a song', () => {
  test('a missing ?src= file is reported on the LCD, and fetched once', async ({ page }) => {
    const missing = '/rebirth_website/archive/does-not-exist.rbs';
    const requests: string[] = [];
    page.on('request', (request) => {
      if (new URL(request.url()).pathname === missing) requests.push(request.url());
    });

    await page.goto(`/rebirth_website/play?src=${encodeURIComponent(missing)}`);

    const player = page.locator('.rbs-player');
    await expect(player).toHaveAttribute('data-load-failure', 'not-found', { timeout: 30_000 });
    // The specific reason has to survive the "error" status; it used to be
    // overwritten with a generic "check console" line.
    await expect(page.locator('#rbsMessage')).toContainText('404');
    expect(requests).toHaveLength(1);
  });

  test('a file dropped as SONG.RBS loads like song.rbs', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await playerReady(page);

    await dropFile(page, 'NOAH-COHN-20.RBS');

    await expect(page.locator('#rbsMetaTitle')).not.toHaveText('—', { timeout: 15_000 });
    await expect(page.locator('#rbsMessage')).toContainText(/Loaded/i);
  });

  test('a dropped file that is neither a song nor a mod is called out, not ignored', async ({
    page,
  }) => {
    await page.goto('/rebirth_website/');
    await playerReady(page);

    await dropFile(page, 'notes.txt');

    await expect(page.locator('#rbsMessage')).toContainText('notes.txt');
    await expect(page.locator('#rbsMessage')).toContainText(/\.rbs song or a \.rbm mod/);
    await expect(page.locator('#rbsMetaTitle')).toHaveText('—');
  });
});
