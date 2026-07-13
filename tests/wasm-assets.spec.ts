import { test, expect } from '@playwright/test';

/**
 * Smoke check: the built WASM artifacts are reachable under the site's base
 * path (`/rb338`). This prevents 404s when the site is deployed under a
 * subpath on GitHub Pages / SFTP.
 */

const BASE = '/rb338';
const WASM_ASSETS = [
  '/wasm/rbsParser.js',
  '/wasm/rbsParser.wasm',
  '/wasm/rbsWorklet.js',
];

test('WASM assets are reachable under the site base path', async ({ request }) => {
  for (const asset of WASM_ASSETS) {
    const url = `${BASE}${asset}`;
    const response = await request.get(url);
    expect(response.ok(), `${url} should be reachable`).toBe(true);
  }
});
