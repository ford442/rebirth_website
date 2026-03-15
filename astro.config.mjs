import { defineConfig } from 'astro/config';

// https://astro.build/config
export default defineConfig({
  site: 'https://ford442.github.io',
  base: '/rebirth_website',
  compressHTML: true,
  build: {
    assets: '_assets',
  },
});
