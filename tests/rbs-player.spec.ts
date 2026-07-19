import { test, expect } from '@playwright/test';

test.describe('RbsPlayer degraded fallback', () => {
  test('shows distinct messaging when WASM assets are missing', async ({ page }) => {
    await page.goto('/rb338/');
    await page.waitForSelector('.rbs-player');

    const player = page.locator('.rbs-player');
    await expect(player).toHaveAttribute('data-player-mode', /degraded-/);

    const fallback = page.locator('#rbsFallback');
    await expect(fallback).toBeVisible();
    await expect(fallback).toHaveAttribute('data-failure-reason', 'wasm-unavailable');
    await expect(page.locator('#rbsFallbackDetail')).toContainText('WASM engine binaries are not built yet');

    await expect(page.locator('#rbsMessage')).toContainText(/degraded|metadata/i);
    await expect(page.locator('#rbsDropZone')).toBeVisible();
    await expect(page.locator('#rbsBtnLoad')).toBeEnabled();
  });

  test('keeps play disabled until a song is loaded in degraded mode', async ({ page }) => {
    await page.goto('/rb338/');
    await page.waitForSelector('.rbs-player[data-player-mode^="degraded"]');

    const playBtn = page.locator('#rbsBtnPlay');
    await expect(playBtn).toBeDisabled();
  });

  test('loads metadata from a fixture and enables sketch play', async ({ page }) => {
    await page.goto('/rb338/');
    await page.waitForSelector('.rbs-player[data-player-mode^="degraded"]');

    const fixtureUrl = '/rb338/wasm/test-fixtures/standard-rebirth.rbs';
    const response = await page.request.get(fixtureUrl);
    test.skip(!response.ok(), 'Parser fixture not available in this checkout');

    await page.evaluate(async (url) => {
      const res = await fetch(url);
      const buffer = await res.arrayBuffer();
      const input = document.getElementById('rbsFileInput') as HTMLInputElement;
      const file = new File([buffer], 'standard-rebirth.rbs', { type: 'application/octet-stream' });
      const dataTransfer = new DataTransfer();
      dataTransfer.items.add(file);
      input.files = dataTransfer.files;
      input.dispatchEvent(new Event('change', { bubbles: true }));
    }, fixtureUrl);

    await expect(page.locator('#rbsMetaTitle')).not.toHaveText('—');
    await expect(page.locator('#rbsMessage')).toContainText(/metadata loaded/i);

    const mode = await page.locator('.rbs-player').getAttribute('data-player-mode');
    if (mode === 'degraded-sketch') {
      await expect(page.locator('#rbsBtnPlay')).toBeEnabled();
    }
  });
});
