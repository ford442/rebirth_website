/** @typedef {import('vite-plugin-pwa').VitePWAOptions} VitePWAOptions */

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
            cacheName: 'rb338-wasm',
            expiration: {
              maxEntries: 16,
              maxAgeSeconds: 60 * 60 * 24 * 365,
            },
            cacheableResponse: {
              statuses: [0, 200],
            },
          },
        },
        {
          urlPattern: ({ url }) =>
            url.pathname.startsWith('/rebirth_website/archive/rbs-songs/demo/') &&
            url.pathname.endsWith('.rbs'),
          handler: 'CacheFirst',
          options: {
            cacheName: 'rb338-demo-songs',
            expiration: {
              maxEntries: 32,
              maxAgeSeconds: 60 * 60 * 24 * 365,
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
            url.hostname === 'test.1ink.us' ||
            url.hostname === 'storage.1ink.us',
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
