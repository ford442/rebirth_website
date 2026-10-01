import { defineConfig, devices } from '@playwright/test';

const isCI = !!process.env.CI;
const wasmBuilt = !!process.env.WASM_BUILT;

/**
 * Specs that need the real production preview server: cross-origin isolation
 * (SharedArrayBuffer) plus the built WASM artifacts and a live AudioWorklet.
 * They run only in the `wasm-preview` project, never against the dev server.
 */
const previewSpecs = [
  '**/wasm-engine-preview.spec.ts',
  '**/wasm-rbm-mod.spec.ts',
  '**/wasm-export.spec.ts',
  '**/wasm-automation.spec.ts',
  '**/wasm-heap-probe.spec.ts',
  '**/wasm-step-edit.spec.ts',
  '**/wasm-save-rbs.spec.ts',
];

/** Specs that need built WASM artifacts but run against either server. */
const wasmArtifactSpecs = ['**/wasm-assets.spec.ts', '**/wasm-engine.spec.ts'];

/** ci.yml builds the site without Emscripten; wasm.yml sets WASM_BUILT=1. */
const wasmTestIgnore = isCI && !wasmBuilt ? [...previewSpecs, ...wasmArtifactSpecs] : [];

const mainURL = 'http://127.0.0.1:4321';

/**
 * CI already serves the production preview on the main port. Locally the main
 * server is `astro dev`, so a WASM_BUILT=1 run (after `npm run build:ship`)
 * starts a second, production preview server for the `wasm-preview` project.
 */
const separatePreview = !isCI && wasmBuilt;
const previewURL = separatePreview ? 'http://127.0.0.1:4322' : mainURL;

export default defineConfig({
  testDir: './tests',
  fullyParallel: true,
  forbidOnly: isCI,
  retries: isCI ? 2 : 0,
  workers: isCI ? 1 : undefined,
  reporter: isCI ? [['github'], ['html', { open: 'never' }]] : 'html',
  use: {
    baseURL: mainURL,
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
      testIgnore: [...previewSpecs, ...wasmTestIgnore],
      use: { ...devices['Desktop Chrome'] },
    },
    {
      name: 'wasm-preview',
      testMatch: previewSpecs,
      testIgnore: wasmTestIgnore,
      use: {
        ...devices['Desktop Chrome'],
        baseURL: previewURL,
        serviceWorkers: 'allow',
      },
    },
    ...(isCI
      ? []
      : [
          {
            name: 'mobile-chrome',
            testIgnore: [...previewSpecs, ...wasmTestIgnore],
            use: { ...devices['Pixel 5'] },
          },
          {
            name: 'mobile-safari',
            testIgnore: [...previewSpecs, ...wasmTestIgnore],
            use: { ...devices['iPhone 12'] },
          },
          {
            name: 'tablet',
            testIgnore: [...previewSpecs, ...wasmTestIgnore],
            use: { ...devices['iPad Pro'] },
          },
        ]),
  ],

  webServer: [
    {
      command: isCI ? 'npm run preview -- --host 127.0.0.1' : 'npm run dev -- --host 127.0.0.1',
      url: `${mainURL}/rebirth_website/`,
      reuseExistingServer: !isCI,
      timeout: 120 * 1000,
    },
    ...(separatePreview
      ? [
          {
            command: 'npm run preview -- --host 127.0.0.1 --port 4322',
            url: `${previewURL}/rebirth_website/`,
            reuseExistingServer: false,
            timeout: 120 * 1000,
          },
        ]
      : []),
  ],
});
