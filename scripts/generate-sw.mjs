#!/usr/bin/env node
/**
 * Generate the production service worker after `astro build`.
 * Astro 6 does not trigger vite-plugin-pwa closeBundle; run this post-build.
 */
import { generateSW } from 'workbox-build';
import { createPwaOptions, PWA_BASE } from '../pwa.config.mjs';

const opts = createPwaOptions();
const basePrefix = PWA_BASE.endsWith('/') ? PWA_BASE : `${PWA_BASE}/`;

const result = await generateSW({
  ...opts.workbox,
  swDest: 'dist/sw.js',
  globDirectory: 'dist',
  importScripts: ['coi-serviceworker.js'],
  modifyURLPrefix: {
    '': basePrefix,
  },
  maximumFileSizeToCacheInBytes: 5 * 1024 * 1024,
});

console.log(`Service worker: ${result.filePaths.join(', ')}`);
console.log(`Precache: ${result.count} files (${Math.round(result.size / 1024)} KiB)`);
if (result.warnings?.length) {
  for (const warning of result.warnings) {
    console.warn(`workbox: ${warning}`);
  }
}
