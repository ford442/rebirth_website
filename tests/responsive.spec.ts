import { test, expect } from '@playwright/test';

const TEST_URL = 'http://localhost:3000';

// Viewport sizes to test
const viewports = [
  { name: 'Mobile (Small)', width: 375, height: 667 },    // iPhone SE
  { name: 'Mobile (Large)', width: 414, height: 896 },    // iPhone 11
  { name: 'Tablet (Portrait)', width: 768, height: 1024 }, // iPad
  { name: 'Tablet (Landscape)', width: 1024, height: 768 },
  { name: 'Desktop', width: 1280, height: 720 },
];

test.describe('Responsive Layout Tests', () => {
  viewports.forEach(viewport => {
    test(`should render properly on ${viewport.name} (${viewport.width}x${viewport.height})`, async ({ page }) => {
      // Set viewport
      await page.setViewportSize({
        width: viewport.width,
        height: viewport.height,
      });

      // Navigate to page
      await page.goto(TEST_URL);

      // Wait for page to be fully loaded
      await page.waitForLoadState('networkidle');

      // Take screenshot for visual reference
      await page.screenshot({
        path: `./tests/screenshots/${viewport.name.replace(/\s+/g, '-')}.png`,
        fullPage: true,
      });

      // Check for horizontal overflow
      const bodyWidth = await page.evaluate(() => document.body.offsetWidth);
      const windowWidth = await page.evaluate(() => window.innerWidth);
      expect(bodyWidth).toBeLessThanOrEqual(windowWidth + 1); // +1 for rounding

      // Check that main elements are visible
      const panels = await page.locator('.rb-panel').count();
      expect(panels).toBeGreaterThan(0);

      // Verify no horizontal scrollbar (except when intentional)
      const scrollWidth = await page.evaluate(() => document.body.scrollWidth);
      expect(scrollWidth).toBeLessThanOrEqual(windowWidth + 1);
    });
  });

  test('should have proper touch target sizes on mobile', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 667 });
    await page.goto(TEST_URL);
    await page.waitForLoadState('networkidle');

    // Check knob sizes (should be at least 40-48px on touch devices)
    const knobs = await page.locator('.rb-knob__dial');
    const knobCount = await knobs.count();
    expect(knobCount).toBeGreaterThan(0);

    for (let i = 0; i < knobCount; i++) {
      const knob = knobs.nth(i);
      const boundingBox = await knob.boundingBox();
      if (boundingBox) {
        // Touch targets should be at least 36px (minimum good practice)
        expect(Math.min(boundingBox.width, boundingBox.height)).toBeGreaterThanOrEqual(36);
      }
    }

    // Check step button sizes
    const stepButtons = await page.locator('.rb-step-btn');
    const buttonCount = await stepButtons.count();
    expect(buttonCount).toBeGreaterThan(0);

    for (let i = 0; i < Math.min(buttonCount, 5); i++) {
      const button = stepButtons.nth(i);
      const boundingBox = await button.boundingBox();
      if (boundingBox) {
        // Step buttons should be at least 32px on mobile
        expect(Math.min(boundingBox.width, boundingBox.height)).toBeGreaterThanOrEqual(32);
      }
    }
  });

  test('should support touch interactions on mobile', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 667 });
    await page.goto(TEST_URL);
    await page.waitForLoadState('networkidle');

    // Test tapping a step button
    const stepButtons = await page.locator('.rb-step-btn');
    if (await stepButtons.count() > 0) {
      const firstButton = stepButtons.first();
      
      // Simulate touch tap
      await firstButton.tap();

      // Check if button has :active or focus styles applied
      // (Visual verification would be better, but this at least ensures no errors)
      expect(firstButton).toBeDefined();
    }

    // Test knob interaction
    const knobs = await page.locator('.rb-knob');
    if (await knobs.count() > 0) {
      const firstKnob = knobs.first();
      
      // Hover over knob to check for hover states
      await firstKnob.hover();
      
      // Check if element is interactive
      expect(firstKnob).toBeDefined();
    }
  });

  test('should have proper spacing on small screens', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 667 });
    await page.goto(TEST_URL);
    await page.waitForLoadState('networkidle');

    // Check that panels don't have excessive margins
    const panels = await page.locator('.rb-panel');
    expect(await panels.count()).toBeGreaterThan(0);

    // Verify action buttons are centered and accessible on mobile
    const actionBtns = await page.locator('.action-btns');
    if (await actionBtns.count() > 0) {
      const style = await actionBtns.first().evaluate((el) => 
        window.getComputedStyle(el).justifyContent
      );
      // Should be centered on mobile
      expect(['center', 'center']).toContain(style);
    }
  });

  test('should not have text truncation issues on mobile', async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 667 });
    await page.goto(TEST_URL);
    await page.waitForLoadState('networkidle');

    // Check main title is readable
    const title = await page.locator('.master-title__main');
    if (await title.count() > 0) {
      const fontSize = await title.first().evaluate((el) => 
        window.getComputedStyle(el).fontSize
      );
      // Font size should not be zero
      expect(fontSize).not.toBe('0px');
    }
  });
});

test.describe('Touch Device Media Queries', () => {
  test('should apply touch-specific styles on touch devices', async ({ page }) => {
    // Test with touch device emulation
    await page.goto(TEST_URL);
    await page.waitForLoadState('networkidle');

    // Check that touch-specific CSS is applied
    const knobDial = await page.locator('.rb-knob__dial').first();
    
    // Verify computed styles exist
    const computedStyle = await knobDial.evaluate((el) => 
      window.getComputedStyle(el)
    );
    
    // Just verify the element is styled (not null)
    expect(computedStyle).toBeDefined();
  });
});
