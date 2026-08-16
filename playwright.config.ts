import { defineConfig, devices } from '@playwright/test';

const isCI = !!process.env.CI;
const wasmBuilt = !!process.env.WASM_BUILT;

const wasmTestIgnore =
  isCI && !wasmBuilt
    ? [
        'tests/wasm-assets.spec.ts',
        'tests/wasm-engine.spec.ts',
        'tests/wasm-engine-preview.spec.ts',
      ]
    : [];

export default defineConfig({
  testDir: './tests',
  fullyParallel: true,
  forbidOnly: isCI,
  retries: isCI ? 2 : 0,
  workers: isCI ? 1 : undefined,
  reporter: isCI ? [['github'], ['html', { open: 'never' }]] : 'html',
  use: {
    baseURL: 'http://127.0.0.1:4321',
    trace: 'on-first-retry',
    serviceWorkers: 'block',
    launchOptions: {
      args: ['--autoplay-policy=no-user-gesture-required'],
    },
  },

  testIgnore: wasmTestIgnore,

  projects: [
    {
      name: 'chromium',
      testIgnore: ['**/wasm-engine-preview.spec.ts', ...wasmTestIgnore],
      use: { ...devices['Desktop Chrome'] },
    },
    {
      name: 'wasm-preview',
      testMatch: '**/wasm-engine-preview.spec.ts',
      testIgnore: wasmTestIgnore,
      use: {
        ...devices['Desktop Chrome'],
        serviceWorkers: 'allow',
      },
      webServer: {
        command: 'npm run preview -- --host 127.0.0.1',
        url: 'http://127.0.0.1:4321/rebirth_website/',
        reuseExistingServer: false,
        timeout: 120 * 1000,
      },
    },
    ...(isCI
      ? []
      : [
          {
            name: 'mobile-chrome',
            testIgnore: ['**/wasm-engine-preview.spec.ts', ...wasmTestIgnore],
            use: { ...devices['Pixel 5'] },
          },
          {
            name: 'mobile-safari',
            testIgnore: ['**/wasm-engine-preview.spec.ts', ...wasmTestIgnore],
            use: { ...devices['iPhone 12'] },
          },
          {
            name: 'tablet',
            testIgnore: ['**/wasm-engine-preview.spec.ts', ...wasmTestIgnore],
            use: { ...devices['iPad Pro'] },
          },
        ]),
  ],

  webServer: {
    command: isCI
      ? 'npm run preview -- --host 127.0.0.1'
      : 'npm run dev -- --host 127.0.0.1',
    url: 'http://127.0.0.1:4321/rebirth_website/',
    reuseExistingServer: !isCI,
    timeout: 120 * 1000,
  },
});
