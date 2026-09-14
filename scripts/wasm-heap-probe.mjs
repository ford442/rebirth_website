#!/usr/bin/env node
/**
 * WASM heap probe — measure live+mod+bounce peaks against INITIAL_MEMORY.
 *
 *   npm run wasm:build
 *   HEAP_PROBE_URL=http://127.0.0.1:4321/rebirth_website/ npm run wasm:heap-probe
 *
 * Playwright `tests/wasm-heap-probe.spec.ts` runs the same in-page function.
 * Exit 1 if usedBytes exceeds INITIAL_MEMORY - 8 MiB.
 */

import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from '@playwright/test';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
export const HEAP_SLACK_BYTES = 8 * 1024 * 1024;
const BARS = 32;

const SONG = path.join(ROOT, 'src/wasm/test-fixtures/standard-rebirth.rbs');
const SAMPLE_KIT = path.join(ROOT, 'src/wasm/test-fixtures/mods/sample-kit.rbm');
const LARGE_KIT = path.join(ROOT, 'src/wasm/test-fixtures/mods/large-kit.rbm');
const MANIFEST = path.join(ROOT, 'public/wasm/wasm-build.json');

function die(message) {
  console.error(message);
  process.exit(1);
}

export function generateLargeKit() {
  const result = spawnSync('python3', [path.join(ROOT, 'scripts/make-rbm-large-kit-fixture.py')], {
    cwd: ROOT,
    encoding: 'utf8',
  });
  if (result.status !== 0) {
    die(`large-kit generator failed:\n${result.stderr || result.stdout}`);
  }
}

export function slackLimit(initialMemory) {
  return initialMemory - HEAP_SLACK_BYTES;
}

export function fixturePayloads() {
  generateLargeKit();
  return {
    songB64: readFileSync(SONG).toString('base64'),
    sampleKitB64: readFileSync(SAMPLE_KIT).toString('base64'),
    largeKitB64: readFileSync(LARGE_KIT).toString('base64'),
    bars: BARS,
  };
}

export async function runHeapProbeInPage(page, args) {
  return page.evaluate(async ({ songB64, sampleKitB64, largeKitB64, bars }) => {
    const b64ToBuf = (b64) => {
      const bin = atob(b64);
      const out = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i += 1) out[i] = bin.charCodeAt(i);
      return out.buffer;
    };

    const WasmAudioBridge = window.WasmAudioBridge;
    const bridge = new WasmAudioBridge();
    await bridge.init();
    const module = bridge.wasmModule;
    if (!module?.heapStats) throw new Error('heapStats() is not exported');

    const snap = () => module.heapStats();
    const peaks = {};
    peaks.afterInit = snap();

    await bridge.loadRbsFile(b64ToBuf(songB64));
    peaks.afterSong = snap();

    await bridge.loadRbmFile(b64ToBuf(sampleKitB64));
    peaks.afterSampleKit = snap();

    await bridge.loadRbmFile(b64ToBuf(largeKitB64));
    peaks.afterLargeKit = snap();

    const sampleRate = bridge.ctx?.sampleRate ?? 44100;
    const bpm = bridge.getTempoBpm() || 150;
    const frames = Math.round(sampleRate * (60 / bpm) * 4 * bars);

    // Legacy same-module dual engine (ADR 0002 production bug).
    const dual = new module.RbsAudioEngine();
    dual.init({
      sampleRate,
      bufferSize: 128,
      enableTb303A: true,
      enableTb303B: true,
      enableTr808: true,
      enableTr909: true,
      enableDistortion: true,
      enableCompressor: true,
      enableDelay: true,
    });
    const songU8 = new Uint8Array(b64ToBuf(songB64));
    const songPtr = module._malloc(songU8.byteLength);
    if (songPtr === 0) throw new Error('malloc(0) song dual');
    module.HEAPU8.set(songU8, songPtr);
    dual.loadSongFromBytes(songPtr, songU8.byteLength);
    module._free(songPtr);
    const kitU8 = new Uint8Array(b64ToBuf(largeKitB64));
    const kitPtr = module._malloc(kitU8.byteLength);
    if (kitPtr === 0) throw new Error('malloc(0) mod dual');
    module.HEAPU8.set(kitU8, kitPtr);
    dual.loadMod(kitPtr, kitU8.byteLength);
    module._free(kitPtr);
    peaks.legacyDualBeforeWav = snap();
    const legacyWav = dual.renderOfflineToWav(frames, 255);
    peaks.legacyDualDuringWav = snap();
    dual.delete();
    peaks.afterLegacyCleanup = snap();

    bridge.play();
    const bounced = await bridge.bounceToWav(frames);
    peaks.liveDuringWorkerBounce = snap();
    bridge.stop();
    bridge.dispose();

    return {
      peaks,
      frames,
      legacyWavBytes: legacyWav.byteLength,
      bounceWavBytes: bounced.bytes.byteLength,
    };
  }, args);
}

async function main() {
  if (!existsSync(MANIFEST)) {
    die('Missing public/wasm/wasm-build.json — run npm run wasm:build first.');
  }
  const manifest = JSON.parse(readFileSync(MANIFEST, 'utf8'));
  const initialMemory = Number(manifest.build?.initialMemory);
  if (!Number.isFinite(initialMemory) || initialMemory <= 0) {
    die('wasm-build.json is missing build.initialMemory');
  }

  const url = process.env.HEAP_PROBE_URL;
  if (!url) {
    generateLargeKit();
    console.log(
      JSON.stringify(
        {
          initialMemory,
          slackLimit: slackLimit(initialMemory),
          note: 'Set HEAP_PROBE_URL to run the browser probe (Playwright wasm-preview).',
        },
        null,
        2
      )
    );
    return;
  }

  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto(url, { waitUntil: 'networkidle' });
  await page.waitForFunction(() => !!window.WasmAudioBridge, null, { timeout: 15000 });

  const result = await runHeapProbeInPage(page, fixturePayloads());
  await browser.close();

  const livePeak = Math.max(
    result.peaks.afterInit.usedBytes,
    result.peaks.afterSong.usedBytes,
    result.peaks.afterSampleKit.usedBytes,
    result.peaks.afterLargeKit.usedBytes,
    result.peaks.afterLegacyCleanup.usedBytes,
    result.peaks.liveDuringWorkerBounce.usedBytes
  );
  const legacyPeak = result.peaks.legacyDualDuringWav.usedBytes;
  const limit = slackLimit(initialMemory);
  const report = {
    initialMemory,
    slackBytes: HEAP_SLACK_BYTES,
    limit,
    livePeak,
    legacyDualPeak: legacyPeak,
    bounceWavBytes: result.bounceWavBytes,
    frames: result.frames,
    peaks: result.peaks,
  };
  writeFileSync(path.join(ROOT, 'public/wasm/wasm-heap-probe.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));

  // Shipping path is Worker bounce: gate the live heap. Also fail if even the
  // isolated live+mod footprint already exceeds slack (would need a heap bump).
  if (livePeak > limit) {
    die(`Heap probe failed: livePeak=${livePeak} exceeds limit ${limit} (INITIAL_MEMORY - 8 MiB)`);
  }
}

const invoked = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (invoked) {
  main().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
