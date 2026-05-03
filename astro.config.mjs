import { defineConfig } from 'astro/config';

// https://astro.build/config
export default defineConfig({
  site: 'https://ford442.github.io',
  base: '/rb338',
  compressHTML: true,
  build: {
    assets: '_assets',
  },
});
