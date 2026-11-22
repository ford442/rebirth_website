/** @typedef {import('vite-plugin-pwa').VitePWAOptions} VitePWAOptions */

import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

/**
 * Cache name for the WASM runtime cache, keyed by the content of the glue and
 * the binary this build ships.
 *
 * `/wasm/*` names are not content-hashed, so with one fixed cache name a
 * CacheFirst entry could pair a new `rbsParser.js` with last deploy's
 * `rbsParser.wasm` (or the reverse) once the precache stops shadowing them —
 * and an Embind glue/binary mismatch fails at instantiation. Keying the cache
 * by both files' bytes makes every build's pair its own cache.
 */
export function wasmCacheName() {
  const dir = fileURLToPath(new URL('./public/wasm/', import.meta.url));
  const hash = createHash('sha256');
  let found = false;
  for (const file of ['rbsParser.js', 'rbsParser.wasm']) {
    const path = `${dir}${file}`;
    if (!existsSync(path)) continue;
    hash.update(readFileSync(path));
    found = true;
  }
  return found ? `rb338-wasm-${hash.digest('hex').slice(0, 12)}` : 'rb338-wasm-dev';
}

/** @type {string} Must match astro.config.mjs `base`. */
export const PWA_BASE = '/rebirth_website';

/** @returns {VitePWAOptions} */
export function createPwaOptions() {
  const base = PWA_BASE.endsWith('/') ? PWA_BASE : `${PWA_BASE}/`;

  return {
    registerType: 'prompt',
    injectRegister: false,
    includeAssets: [
      'icons/icon.svg',
      'icons/icon-192.png',
      'icons/icon-512.png',
      'icons/icon-maskable-512.png',
      'icons/apple-touch-icon.png',
    ],
    manifest: {
      name: 'ReBirth RB-338 Archive',
      short_name: 'RB-338 Archive',
      description:
        'Community archive for the ReBirth RB-338 synthesizer — songs, mods, history, and in-browser playback.',
      theme_color: '#111111',
      background_color: '#111111',
      display: 'standalone',
      orientation: 'any',
      lang: 'en',
      categories: ['music', 'entertainment'],
      start_url: base,
      scope: base,
      id: base,
      icons: [
        {
          src: 'icons/icon-192.png',
          sizes: '192x192',
          type: 'image/png',
        },
        {
          src: 'icons/icon-512.png',
          sizes: '512x512',
          type: 'image/png',
        },
        {
          src: 'icons/icon-maskable-512.png',
          sizes: '512x512',
          type: 'image/png',
          purpose: 'maskable',
        },
      ],
    },
    workbox: {
      globPatterns: ['**/*.{html,css,js,svg,png,ico,json,wasm,woff2,woff,txt,webmanifest}'],
      navigateFallback: `${base}offline/index.html`,
      navigateFallbackDenylist: [/^\/rebirth_website\/api\//, /\/[^/?]+\.[^/]+$/],
      runtimeCaching: [
        {
          urlPattern: ({ url }) => url.pathname.startsWith('/rebirth_website/wasm/'),
          handler: 'CacheFirst',
          options: {
            // Immutable within one build, so CacheFirst is safe — see wasmCacheName().
            cacheName: wasmCacheName(),
            expiration: {
              maxEntries: 16,
              maxAgeSeconds: 60 * 60 * 24 * 30,
              purgeOnQuotaError: true,
            },
            cacheableResponse: {
              statuses: [0, 200],
            },
          },
        },
        {
          urlPattern: ({ url }) =>
            url.pathname.startsWith('/rebirth_website/archive/rbs-songs/') &&
            url.pathname.endsWith('.rbs'),
          handler: 'CacheFirst',
          options: {
            cacheName: 'rb338-demo-songs',
            expiration: {
              maxEntries: 64,
              maxAgeSeconds: 60 * 60 * 24 * 365,
            },
            cacheableResponse: {
              statuses: [0, 200],
            },
          },
        },
        {
          urlPattern: ({ url }) => url.pathname === '/rebirth_website/data/archive-core.json',
          handler: 'CacheFirst',
          options: {
            cacheName: 'rb338-data-json',
            expiration: {
              maxEntries: 8,
              maxAgeSeconds: 60 * 60 * 24 * 7,
            },
            cacheableResponse: {
              statuses: [0, 200],
            },
          },
        },
        {
          urlPattern: ({ url }) => url.pathname.startsWith('/rebirth_website/data/'),
          handler: 'CacheFirst',
          options: {
            cacheName: 'rb338-data-json',
            expiration: {
              maxEntries: 32,
              maxAgeSeconds: 60 * 60 * 24 * 7,
            },
            cacheableResponse: {
              statuses: [0, 200],
            },
          },
        },
        {
          urlPattern: ({ url }) =>
            url.hostname === 'test.1ink.us' || url.hostname === 'storage.1ink.us',
          handler: 'NetworkOnly',
        },
        {
          urlPattern: ({ url }) =>
            url.hostname === 'fonts.googleapis.com' || url.hostname === 'fonts.gstatic.com',
          handler: 'StaleWhileRevalidate',
          options: {
            cacheName: 'rb338-google-fonts',
            expiration: {
              maxEntries: 24,
              maxAgeSeconds: 60 * 60 * 24 * 365,
            },
            cacheableResponse: {
              statuses: [0, 200],
            },
          },
        },
      ],
    },
    devOptions: {
      enabled: true,
      navigateFallback: `${base}offline/index.html`,
      suppressWarnings: true,
    },
  };
}
