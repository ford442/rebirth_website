import { defineConfig } from 'astro/config';
import { VitePWA } from 'vite-plugin-pwa';
import { createPwaOptions } from './pwa.config.mjs';

// https://astro.build/config
export default defineConfig({
  site: 'https://ford442.github.io',
  base: '/rebirth_website',
  compressHTML: true,
  build: {
    assets: '_assets',
  },
  server: {
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
  vite: {
    plugins: [
      VitePWA({
        ...createPwaOptions(),
        base: '/rebirth_website/',
      }),
    ],
  },
});
