import { defineConfig, devices } from '@playwright/test';

const isCI = !!process.env.CI;
const wasmBuilt = !!process.env.WASM_BUILT;

export default defineConfig({
  testDir: './tests',
  fullyParallel: true,
  forbidOnly: isCI,
  retries: isCI ? 2 : 0,
  workers: isCI ? 1 : undefined,
  reporter: isCI ? [['github'], ['html', { open: 'never' }]] : 'html',
  use: {
    baseURL: 'http://localhost:4321',
    trace: 'on-first-retry',
  },

  testIgnore:
    isCI && !wasmBuilt
      ? ['tests/wasm-assets.spec.ts', 'tests/wasm-engine.spec.ts']
      : [],

  projects: isCI
    ? [
        {
          name: 'chromium',
          use: { ...devices['Desktop Chrome'] },
        },
      ]
    : [
        {
          name: 'chromium',
          use: { ...devices['Desktop Chrome'] },
        },
        {
          name: 'mobile-chrome',
          use: { ...devices['Pixel 5'] },
        },
        {
          name: 'mobile-safari',
          use: { ...devices['iPhone 12'] },
        },
        {
          name: 'tablet',
          use: {
            ...devices['iPad Pro'],
          },
        },
      ],

  webServer: {
    command: isCI ? 'npm run preview' : 'npm run dev',
    url: 'http://localhost:4321/rb338/',
    reuseExistingServer: !isCI,
    timeout: 120 * 1000,
  },
});
