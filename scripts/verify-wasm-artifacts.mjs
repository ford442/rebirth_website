#!/usr/bin/env node
/**
 * verify-wasm-artifacts.mjs
 *
 * Post-build gate for wasm.yml: the shipping WASM artifacts exist in both
 * public/wasm (build.sh output) and dist/wasm (what Pages deploys), and the
 * manifest matches the Release heap policy from ADR 0002.
 *
 * Usage: node scripts/verify-wasm-artifacts.mjs [dir ...]
 */

import { readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';

const EXPECTED_HEAP_BYTES = 64 * 1024 * 1024;
const REQUIRED_FILES = ['rbsParser.js', 'rbsParser.wasm', 'rbsWorklet.js', 'wasm-build.json'];

const directories = process.argv.length > 2 ? process.argv.slice(2) : ['public/wasm', 'dist/wasm'];
const failures = [];

function check(condition, message) {
  if (!condition) failures.push(message);
}

for (const directory of directories) {
  for (const file of REQUIRED_FILES) {
    const path = join(directory, file);
    let size = 0;
    try {
      size = statSync(path).size;
    } catch {
      // Reported below as missing.
    }
    check(size > 0, `${path} is missing or empty`);
  }

  let build;
  try {
    build = JSON.parse(readFileSync(join(directory, 'wasm-build.json'), 'utf8')).build;
  } catch (err) {
    failures.push(`${directory}/wasm-build.json is unreadable: ${err.message}`);
    continue;
  }
  check(build.mode === 'release', `${directory}: build.mode is ${build.mode}, expected release`);
  check(
    build.initialMemory === EXPECTED_HEAP_BYTES,
    `${directory}: initialMemory is ${build.initialMemory}, expected ${EXPECTED_HEAP_BYTES}`
  );
  check(
    build.maximumMemory === EXPECTED_HEAP_BYTES,
    `${directory}: maximumMemory is ${build.maximumMemory}, expected ${EXPECTED_HEAP_BYTES}`
  );
  check(
    build.allowMemoryGrowth === 0,
    `${directory}: allowMemoryGrowth is ${build.allowMemoryGrowth}, expected 0`
  );

  const worklet = readFileSync(join(directory, 'rbsWorklet.js'), 'utf8').trim();
  check(
    worklet === "import './rbsParser.js';",
    `${directory}/rbsWorklet.js is not the checked-in bootstrap: ${JSON.stringify(worklet)}`
  );

  const glue = readFileSync(join(directory, 'rbsParser.js'), 'utf8');
  check(glue.includes('locateFile('), `${directory}/rbsParser.js no longer contains locateFile(`);
}

if (failures.length > 0) {
  for (const failure of failures) console.error(`✗ ${failure}`);
  process.exit(1);
}
console.log(`✓ WASM artifacts verified in ${directories.join(', ')}`);
