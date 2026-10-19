import { existsSync, readdirSync } from 'node:fs';
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

/**
 * Every `tests/wasm-*.spec.ts` must be routed above. A spec in neither list
 * would run under the plain `chromium` project, where the engine is missing
 * or the page is not cross-origin isolated, and fail for the wrong reason (or
 * pass without exercising anything) — which is how `wasm-save-rbs` slipped.
 */
const unroutedWasmSpecs = (existsSync('./tests') ? readdirSync('./tests') : []).filter(
  (file) =>
    /^wasm-.*\.spec\.ts$/.test(file) &&
    ![...previewSpecs, ...wasmArtifactSpecs].some((pattern) => pattern.endsWith(`/${file}`))
);
if (unroutedWasmSpecs.length > 0) {
  throw new Error(
    `playwright.config.ts: add ${unroutedWasmSpecs.join(', ')} to previewSpecs or wasmArtifactSpecs.`
  );
}

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

/**
 * The player only exposes its `window.*` engine hooks when asked to (see
 * `src/wasm/js/player-test-hooks.ts`), so production visitors get none.
 * Specs ask through localStorage, per origin, instead of each adding `?test`.
 */
const withTestHooks = (origin: string) => ({
  cookies: [],
  origins: [{ origin, localStorage: [{ name: 'rb-test-hooks', value: '1' }] }],
});

export default defineConfig({
  testDir: './tests',
  fullyParallel: true,
  forbidOnly: isCI,
  retries: isCI ? 2 : 0,
  workers: isCI ? 1 : undefined,
  reporter: isCI ? [['github'], ['html', { open: 'never' }]] : 'html',
  use: {
    baseURL: mainURL,
    storageState: withTestHooks(mainURL),
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
        storageState: withTestHooks(previewURL),
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
