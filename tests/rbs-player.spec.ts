import { test, expect } from '@playwright/test';

const LOCAL_DEMO_URL = '/rebirth_website/archive/rbs-songs/demo/noah-cohn-20.rbs';

test.describe('RbsPlayer degraded fallback', () => {
  test.beforeEach(async ({ page }) => {
    await page.route('**/*', (route) => {
      const pathname = new URL(route.request().url()).pathname;
      // Only block shipping WASM artifacts — not /src/wasm/js/*.ts player UI modules.
      if (
        pathname.includes('/rebirth_website/wasm/') ||
        pathname.endsWith('.wasm') ||
        pathname.includes('rbsParser.js') ||
        pathname.includes('rbsWorklet.js')
      ) {
        return route.abort();
      }
      return route.continue();
    });
  });

  test('shows distinct messaging when WASM assets are missing', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');

    const player = page.locator('.rbs-player');
    await expect(player).toHaveAttribute('data-player-mode', /degraded-/);

    const fallback = page.locator('#rbsFallback');
    await expect(fallback).toBeVisible();
    await expect(fallback).toHaveAttribute('data-failure-reason', 'wasm-unavailable');
    await expect(page.locator('#rbsFallbackDetail')).toContainText(
      'WASM engine binaries are not built yet'
    );

    await expect(page.locator('#rbsMessage')).toContainText(/degraded|metadata/i);
    await expect(page.locator('#rbsDropZone')).toBeVisible();
    await expect(page.locator('#rbsBtnLoad')).toBeEnabled();
  });

  test('shows a 16-step studio grid and disables live knobs without WASM', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player[data-player-mode^="degraded"]');

    await expect(page.locator('#rbsStudioGrid [data-studio-step]')).toHaveCount(16);
    await expect(page.locator('#rbsStudio [data-studio-knob="cutoff"]')).toBeDisabled();
    await expect(page.locator('#rbsStudio [data-studio-device="tr808"]')).toBeVisible();

    await page.locator('#rbsStudio [data-studio-device="tr808"]').click();
    await expect(page.locator('#rbsStudio [data-studio-device="tr808"]')).toHaveClass(
      /is-selected/
    );
    await expect(page.locator('[data-knob-wrap="cutoff"]')).toHaveAttribute('hidden', '');
  });

  test('keeps play disabled until a song is loaded in degraded mode', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player[data-player-mode^="degraded"]');

    const playBtn = page.locator('#rbsBtnPlay');
    await expect(playBtn).toBeDisabled();
  });

  test('loads metadata from a local demo fixture and enables sketch play', async ({ page }) => {
    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player[data-player-mode^="degraded"]');

    const response = await page.request.get(LOCAL_DEMO_URL);
    expect(response.ok()).toBeTruthy();

    await page.evaluate(async (url) => {
      const res = await fetch(url);
      const buffer = await res.arrayBuffer();
      const input = document.getElementById('rbsFileInput') as HTMLInputElement;
      const file = new File([buffer], 'noah-cohn-20.rbs', { type: 'application/octet-stream' });
      const dataTransfer = new DataTransfer();
      dataTransfer.items.add(file);
      input.files = dataTransfer.files;
      input.dispatchEvent(new Event('change', { bubbles: true }));
    }, LOCAL_DEMO_URL);

    await expect(page.locator('#rbsMetaTitle')).not.toHaveText('—');
    await expect(page.locator('#rbsMessage')).toContainText(/Loaded/i);

    const mode = await page.locator('.rbs-player').getAttribute('data-player-mode');
    if (mode === 'degraded-sketch') {
      await expect(page.locator('#rbsBtnPlay')).toBeEnabled();
    }
  });

  test('Load Demo fetches same-origin demo without external network', async ({ page }) => {
    await page.route('**/*test.1ink.us/**', (route) => route.abort());

    await page.goto('/rebirth_website/');
    await page.waitForSelector('.rbs-player');

    const demoSelect = page.locator('#rbsDemoSelect');
    await expect(demoSelect.locator('option')).toHaveCount(9); // placeholder + 8 demos

    await demoSelect.selectOption({ index: 1 });
    await page.locator('#rbsBtnDemoLoad').click();

    await expect(page.locator('#rbsMetaTitle')).not.toHaveText('—', { timeout: 15_000 });
    await expect(page.locator('#rbsMessage')).toContainText(/Loaded/i);
  });
});
