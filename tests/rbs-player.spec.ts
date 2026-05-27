import { test, expect } from '@playwright/test';

test.describe('RbsPlayer UI shell', () => {
  test('renders playback controls, demo picker, and fallback messaging', async ({ page }) => {
    await page.goto('http://localhost:3000/rb338');
    await page.waitForLoadState('networkidle');

    const player = page.locator('.rbs-player');
    await expect(player).toBeVisible();

    await expect(page.locator('#rbsDemoSelect')).toBeVisible();
    await expect(page.locator('#rbsBtnDemoLoad')).toBeVisible();
    await expect(page.locator('#rbsVolume')).toBeVisible();
    await expect(page.locator('#rbsTempo')).toBeVisible();

    const fallback = page.locator('#rbsFallback');
    await expect(fallback).toBeVisible();

    await expect(page.locator('#rbsMessage')).toContainText('WASM engine failed to initialise');
  });
});
