#!/usr/bin/env node
/**
 * check-wasm-contract.mjs
 *
 * Structural drift checker for the WASM engine <-> TypeScript contract
 * documented in src/wasm/CONTRACT.md.
 *
 * This is deliberately a plain regex/text checker, not a real C++ or
 * TypeScript parser — it is good enough to catch the failure mode this
 * project actually has (a struct/enum edited in one of the four places
 * — main.cpp, CONTRACT.md, wasm-audio.ts, EngineCommands.h/player-studio.ts
 * — and not the others) without adding a compiler dependency to CI.
 *
 * Checks:
 *   1. `DeviceParamId` (src/wasm/cpp/engine/EngineCommands.h) has the same
 *      names, in the same order (implying the same numeric values), as the
 *      `DeviceParam` object in src/wasm/js/player-studio.ts.
 *   2. Every `value_object<T>(...)` registered in main.cpp has the same set
 *      of field names as:
 *        a. the `struct T { ... };` code block documented under that name
 *           in CONTRACT.md, and
 *        b. the matching TypeScript interface in wasm-audio.ts.
 *   3. Required `RbsAudioEngine` Embind `.function("name")` entries exist in
 *      main.cpp and as methods on `RbsAudioEngineInstance` in wasm-audio.ts.
 *
 * Exit code is non-zero (and CI fails) on any mismatch.
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');

const ENGINE_COMMANDS_H = path.join(ROOT, 'src/wasm/cpp/engine/EngineCommands.h');
const PLAYER_STUDIO_TS = path.join(ROOT, 'src/wasm/js/player-studio.ts');
const MAIN_CPP = path.join(ROOT, 'src/wasm/cpp/main.cpp');
const CONTRACT_MD = path.join(ROOT, 'src/wasm/CONTRACT.md');
/**
 * The shared TS types are split by job (song / engine / config / mod). The
 * checker reads them as one corpus so a type can move between files without
 * breaking the contract check.
 */
const WASM_AUDIO_TS_FILES = [
  'src/wasm/types/wasm-audio-song.ts',
  'src/wasm/types/wasm-audio-engine.ts',
  'src/wasm/types/wasm-audio-config.ts',
  'src/wasm/types/wasm-audio-mod.ts',
].map((p) => path.join(ROOT, p));

const WASM_AUDIO_TS_LABEL = 'src/wasm/types/wasm-audio-*.ts';

/** C++ struct name -> TypeScript interface name in the wasm-audio-*.ts types. */
const STRUCT_TO_TS_INTERFACE = {
  EngineConfig: 'EngineConfig',
  HeapStats: 'HeapStats',
  PlaybackPosition: 'PlaybackPosition',
  StepData: 'WasmStepData',
  PatternRef: 'WasmPatternRef',
  ArrangementBar: 'WasmArrangementBar',
  DeviceState: 'WasmDeviceState',
  Pattern: 'WasmPattern',
  ParsedSong: 'WasmParsedSong',
  DelaySettings: 'WasmDelaySettings',
  PcfSettings: 'WasmPcfSettings',
  DistSettings: 'WasmDistSettings',
  CompSettings: 'WasmCompSettings',
  SongFxSettings: 'WasmSongFxSettings',
  ModSampleReportEntry: 'WasmModSampleReportEntry',
  ModLoadReport: 'WasmModLoadReport',
};

const errors = [];

function read(file) {
  return readFileSync(file, 'utf8');
}

/** Concatenated source of every split wasm-audio type file. */
function readWasmAudioTypes() {
  return WASM_AUDIO_TS_FILES.map((f) => read(f)).join('\n');
}

function relative(file) {
  return path.relative(ROOT, file);
}

function fail(message) {
  errors.push(message);
}

function sameSet(a, b) {
  const sa = [...new Set(a)].sort();
  const sb = [...new Set(b)].sort();
  return sa.length === sb.length && sa.every((v, i) => v === sb[i]);
}

// ── 1. DeviceParamId (C++) vs DeviceParam (TS) ────────────────────────

function parseDeviceParamIdEnum(src) {
  const m = src.match(/enum class DeviceParamId[^{]*\{([\s\S]*?)\};/);
  if (!m) {
    fail(`${relative(ENGINE_COMMANDS_H)}: could not find "enum class DeviceParamId { ... };"`);
    return [];
  }
  const names = [];
  for (const rawLine of m[1].split('\n')) {
    const line = rawLine.trim().replace(/,$/, '');
    if (!line || line.startsWith('//')) continue;
    const name = line.split('=')[0].trim();
    if (name) names.push(name);
  }
  return names;
}

function parseDeviceParamTsObject(src) {
  const m = src.match(/export const DeviceParam = \{([\s\S]*?)\} as const;/);
  if (!m) {
    fail(
      `${relative(PLAYER_STUDIO_TS)}: could not find "export const DeviceParam = { ... } as const;"`
    );
    return [];
  }
  const entries = [];
  const entryRe = /(\w+):\s*(\d+),?/g;
  let match;
  while ((match = entryRe.exec(m[1])) !== null) {
    entries.push({ name: match[1], value: Number(match[2]) });
  }
  return entries;
}

function checkDeviceParamId() {
  const cppNames = parseDeviceParamIdEnum(read(ENGINE_COMMANDS_H));
  const tsEntries = parseDeviceParamTsObject(read(PLAYER_STUDIO_TS));

  if (cppNames.length === 0 || tsEntries.length === 0) return;

  if (cppNames.length !== tsEntries.length) {
    fail(
      `DeviceParamId count mismatch: EngineCommands.h has ${cppNames.length} values ` +
        `(${cppNames.join(', ')}), player-studio.ts DeviceParam has ${tsEntries.length} ` +
        `(${tsEntries.map((e) => e.name).join(', ')}).`
    );
    return;
  }

  cppNames.forEach((name, index) => {
    const expected = tsEntries[index];
    if (!expected || expected.name !== name || expected.value !== index) {
      fail(
        `DeviceParamId drift at index ${index}: EngineCommands.h has "${name}" = ${index}, ` +
          `player-studio.ts DeviceParam has ${
            expected ? `"${expected.name}" = ${expected.value}` : '(missing)'
          }.`
      );
    }
  });
}

// ── 2. Embind value_object<T>().field(...) chains ──────────────────────

function parseEmbindValueObjects(src) {
  const objects = new Map();
  const re = /value_object<(\w+)>\("(\w+)"\)([\s\S]*?);/g;
  let match;
  while ((match = re.exec(src)) !== null) {
    const [, structName, embindName, chain] = match;
    const fields = [];
    const fieldRe = /\.field\("(\w+)"/g;
    let fieldMatch;
    while ((fieldMatch = fieldRe.exec(chain)) !== null) {
      fields.push(fieldMatch[1]);
    }
    objects.set(structName, { embindName, fields });
  }
  return objects;
}

function extractBracedStructBody(src, structName) {
  const marker = `struct ${structName} {`;
  const start = src.indexOf(marker);
  if (start === -1) return null;
  const end = src.indexOf('\n};', start);
  if (end === -1) return null;
  return src.slice(start + marker.length, end);
}

function fieldNamesFromCppStructBody(body) {
  const names = [];
  for (const rawLine of body.split('\n')) {
    const line = rawLine.trim();
    if (!line.endsWith(';') || line.startsWith('//')) continue;
    let decl = line.slice(0, -1);
    const eqIdx = decl.indexOf('=');
    if (eqIdx !== -1) decl = decl.slice(0, eqIdx);
    const m = decl.trim().match(/(\w+)$/);
    if (m) names.push(m[1]);
  }
  return names;
}

function extractBracedInterfaceBody(src, interfaceName) {
  const marker = `interface ${interfaceName} {`;
  const start = src.indexOf(marker);
  if (start === -1) return null;
  let depth = 0;
  let i = start + marker.length - 1; // position of the opening brace
  for (; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}') {
      depth--;
      if (depth === 0) break;
    }
  }
  return src.slice(start + marker.length, i);
}

function fieldNamesFromTsInterfaceBody(body) {
  const names = [];
  for (const rawLine of body.split('\n')) {
    const line = rawLine.trim();
    if (!line || line.startsWith('//') || line.startsWith('*') || line.startsWith('/*')) continue;
    const m = line.match(/^(\w+)\??\s*:/);
    if (m) names.push(m[1]);
  }
  return names;
}

function checkValueObjects() {
  const mainCppSrc = read(MAIN_CPP);
  const contractSrc = read(CONTRACT_MD);
  const wasmAudioSrc = readWasmAudioTypes();

  const embindObjects = parseEmbindValueObjects(mainCppSrc);
  if (embindObjects.size === 0) {
    fail(`${relative(MAIN_CPP)}: found no "value_object<T>(\\"Name\\").field(...)" registrations.`);
    return;
  }

  for (const [structName, { fields: embindFields }] of embindObjects) {
    if (embindFields.length === 0) continue; // e.g. structs registered without .field() chains

    // (a) CONTRACT.md documented struct shape
    const contractBody = extractBracedStructBody(contractSrc, structName);
    if (!contractBody) {
      fail(
        `CONTRACT.md is missing a "struct ${structName} { ... };" code block for the ` +
          `Embind-registered type "${structName}" (main.cpp).`
      );
    } else {
      const contractFields = fieldNamesFromCppStructBody(contractBody);
      if (!sameSet(contractFields, embindFields)) {
        fail(
          `Field mismatch for "${structName}": main.cpp Embind fields = ` +
            `[${embindFields.join(', ')}], CONTRACT.md struct fields = [${contractFields.join(', ')}].`
        );
      }
    }

    // (b) wasm-audio.ts TypeScript interface shape
    const tsInterfaceName = STRUCT_TO_TS_INTERFACE[structName];
    if (!tsInterfaceName) {
      fail(
        `scripts/check-wasm-contract.mjs has no STRUCT_TO_TS_INTERFACE entry for "${structName}" ` +
          `— add one (or the TS interface it should match) so this checker can verify it.`
      );
      continue;
    }
    const tsBody = extractBracedInterfaceBody(wasmAudioSrc, tsInterfaceName);
    if (!tsBody) {
      fail(
        `${WASM_AUDIO_TS_LABEL}: missing "interface ${tsInterfaceName} { ... }" for the ` +
          `Embind-registered type "${structName}" (main.cpp).`
      );
      continue;
    }
    const tsFields = fieldNamesFromTsInterfaceBody(tsBody);
    if (!sameSet(tsFields, embindFields)) {
      fail(
        `Field mismatch for "${structName}" / "${tsInterfaceName}": main.cpp Embind fields = ` +
          `[${embindFields.join(', ')}], ${WASM_AUDIO_TS_LABEL} interface fields = [${tsFields.join(', ')}].`
      );
    }
  }
}

/** Archive/load path names that must stay on RbsAudioEngine Embind. */
const REQUIRED_ENGINE_EMBIND_FUNCTIONS = [
  'init',
  'loadSong',
  'loadSongFromBytes',
  'lastParseError',
  'loadMod',
  'play',
  'stop',
  'setDeviceParam',
  'renderOfflineToWav',
  'songLengthFrames',
  'getPlaybackPosition',
];

function parseClassEmbindFunctions(src, className) {
  const re = new RegExp(`class_<${className}>\\("${className}"\\)([\\s\\S]*?);`);
  const m = src.match(re);
  if (!m) return null;
  const names = [];
  const fnRe = /\.function\("(\w+)"/g;
  let match;
  while ((match = fnRe.exec(m[1])) !== null) {
    names.push(match[1]);
  }
  return names;
}

function checkEngineFunctions() {
  const names = parseClassEmbindFunctions(read(MAIN_CPP), 'RbsAudioEngine');
  if (!names) {
    fail(`${relative(MAIN_CPP)}: could not find class_<RbsAudioEngine>("RbsAudioEngine") chain.`);
    return;
  }
  for (const required of REQUIRED_ENGINE_EMBIND_FUNCTIONS) {
    if (!names.includes(required)) {
      fail(
        `RbsAudioEngine Embind is missing .function("${required}") in ${relative(MAIN_CPP)} ` +
          `(required by scripts/check-wasm-contract.mjs).`
      );
    }
  }

  const tsSrc = readWasmAudioTypes();
  const tsBody = extractBracedInterfaceBody(tsSrc, 'RbsAudioEngineInstance');
  if (!tsBody) {
    fail(`${WASM_AUDIO_TS_LABEL}: missing interface RbsAudioEngineInstance.`);
    return;
  }
  const tsMethods = [];
  for (const rawLine of tsBody.split('\n')) {
    const line = rawLine.trim();
    if (!line || line.startsWith('//') || line.startsWith('*') || line.startsWith('/*')) continue;
    const m = line.match(/^(\w+)\s*\(/);
    if (m) tsMethods.push(m[1]);
  }
  for (const required of REQUIRED_ENGINE_EMBIND_FUNCTIONS) {
    if (!tsMethods.includes(required)) {
      fail(`RbsAudioEngineInstance is missing "${required}(...)" in ${WASM_AUDIO_TS_LABEL}.`);
    }
  }
}

checkDeviceParamId();
checkValueObjects();
checkEngineFunctions();

if (errors.length > 0) {
  console.error(`\n✗ WASM contract check found ${errors.length} problem(s):\n`);
  for (const err of errors) {
    console.error(`  - ${err}`);
  }
  console.error(
    '\nSee src/wasm/CONTRACT.md — the C++ struct / Embind registration / TypeScript ' +
      'interface / CONTRACT.md documentation must all agree.\n'
  );
  process.exit(1);
}

console.log('✓ WASM engine <-> TypeScript contract check passed.');
