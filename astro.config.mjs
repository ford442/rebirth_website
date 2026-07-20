import { defineConfig } from 'astro/config';
import { VitePWA } from 'vite-plugin-pwa';
import { createPwaOptions } from './pwa.config.mjs';

// https://astro.build/config
export default defineConfig({
  site: 'https://ford442.github.io',
  base: '/rb338',
  compressHTML: true,
  build: {
    assets: '_assets',
  },
  vite: {
    plugins: [
      VitePWA({
        ...createPwaOptions(),
        base: '/rb338/',
      }),
    ],
  },
});
