#!/usr/bin/env node
/**
 * Generate PNG PWA icons from public/icons/icon.svg.
 * Run after editing the SVG: node scripts/generate-pwa-icons.mjs
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const svgPath = path.join(root, 'public/icons/icon.svg');
const outDir = path.join(root, 'public/icons');
const svg = fs.readFileSync(svgPath);

const sizes = [
  { name: 'icon-192.png', size: 192 },
  { name: 'icon-512.png', size: 512 },
  { name: 'apple-touch-icon.png', size: 180 },
  { name: 'icon-maskable-512.png', size: 512, maskable: true },
];

async function main() {
  for (const entry of sizes) {
    let pipeline = sharp(svg).resize(entry.size, entry.size);
    if (entry.maskable) {
      pipeline = sharp({
        create: {
          width: entry.size,
          height: entry.size,
          channels: 4,
          background: '#111111',
        },
      }).composite([
        {
          input: await sharp(svg).resize(Math.round(entry.size * 0.72)).png().toBuffer(),
          gravity: 'center',
        },
      ]);
    }
    const outPath = path.join(outDir, entry.name);
    await pipeline.png().toFile(outPath);
    console.log(`Wrote ${outPath}`);
  }
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
