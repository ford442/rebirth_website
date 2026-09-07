import { defineConfig, devices } from '@playwright/test';

const isCI = !!process.env.CI;
const wasmBuilt = !!process.env.WASM_BUILT;

/**
 * Specs that need the real production preview server: cross-origin isolation
 * (SharedArrayBuffer) plus the built WASM artifacts and a live AudioWorklet.
 * They run only in the `wasm-preview` project, never against the dev server.
 */
const preferPreviewSpecs = [
  '**/wasm-engine-preview.spec.ts',
  '**/wasm-rbm-mod.spec.ts',
  '**/wasm-export.spec.ts',
];

const wasmTestIgnore =
  isCI && !wasmBuilt
    ? [
        'tests/wasm-assets.spec.ts',
        'tests/wasm-engine.spec.ts',
        'tests/wasm-engine-preview.spec.ts',
        'tests/wasm-rbm-mod.spec.ts',
        'tests/wasm-export.spec.ts',
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
      testIgnore: [...preferPreviewSpecs, ...wasmTestIgnore],
      use: { ...devices['Desktop Chrome'] },
    },
    {
      name: 'wasm-preview',
      testMatch: preferPreviewSpecs,
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
            testIgnore: [...preferPreviewSpecs, ...wasmTestIgnore],
            use: { ...devices['Pixel 5'] },
          },
          {
            name: 'mobile-safari',
            testIgnore: [...preferPreviewSpecs, ...wasmTestIgnore],
            use: { ...devices['iPhone 12'] },
          },
          {
            name: 'tablet',
            testIgnore: [...preferPreviewSpecs, ...wasmTestIgnore],
            use: { ...devices['iPad Pro'] },
          },
        ]),
  ],

  webServer: {
    command: isCI ? 'npm run preview -- --host 127.0.0.1' : 'npm run dev -- --host 127.0.0.1',
    url: 'http://127.0.0.1:4321/rebirth_website/',
    reuseExistingServer: !isCI,
    timeout: 120 * 1000,
  },
});
