import { test, expect } from '@playwright/test';

const BASE = '/rebirth_website';
const EMSCRIPTEN_VERSION = '6.0.3';
const ARTIFACT_KEYS = ['glue', 'wasm', 'worklet'] as const;

interface WasmBuildManifest {
  emscripten: {
    pinned: string;
    installed: string;
  };
  files: Record<(typeof ARTIFACT_KEYS)[number], { path: string; bytes: number }>;
}

test('WASM manifest and declared assets are valid under the site base path', async ({
  request,
}) => {
  const manifestUrl = `${BASE}/wasm/wasm-build.json`;
  const manifestResponse = await request.get(manifestUrl);
  expect(manifestResponse.ok(), `${manifestUrl} should be reachable`).toBe(true);

  const manifest = (await manifestResponse.json()) as WasmBuildManifest;
  expect(manifest.emscripten).toEqual({
    pinned: EMSCRIPTEN_VERSION,
    installed: EMSCRIPTEN_VERSION,
  });

  for (const key of ARTIFACT_KEYS) {
    const artifact = manifest.files[key];
    expect(artifact.path).toMatch(/^[A-Za-z0-9._-]+$/);
    expect(artifact.bytes).toBeGreaterThan(0);

    const url = `${BASE}/wasm/${artifact.path}`;
    const response = await request.get(url);
    expect(response.ok(), `${url} should be reachable`).toBe(true);
    expect((await response.body()).byteLength, `${url} should match its manifest size`).toBe(
      artifact.bytes
    );
  }
});

test('built homepage reports the WASM engine as ready', async ({ page }) => {
  await page.goto(`${BASE}/`);
  await expect(page.getByText('WASM READY', { exact: true })).toBeVisible();
  await expect(page.getByLabel('System status terminal')).toContainText('wasm   : ready');
});
