import { readFileSync, statSync } from 'node:fs';
import { resolve } from 'node:path';

const ARTIFACT_KEYS = ['glue', 'wasm', 'worklet'] as const;

interface WasmArtifactRecord {
  path: string;
  bytes: number;
}

interface WasmBuildManifest {
  emscripten: {
    pinned: string;
    installed: string;
  };
  files: Record<(typeof ARTIFACT_KEYS)[number], WasmArtifactRecord>;
}

export interface WasmBuildStatus {
  ready: boolean;
  manifest: WasmBuildManifest | null;
}

function isArtifactRecord(value: unknown): value is WasmArtifactRecord {
  if (!value || typeof value !== 'object') return false;
  const record = value as Record<string, unknown>;
  return (
    typeof record.path === 'string' &&
    /^[A-Za-z0-9._-]+$/.test(record.path) &&
    Number.isSafeInteger(record.bytes) &&
    Number(record.bytes) > 0
  );
}

function parseManifest(value: unknown): WasmBuildManifest | null {
  if (!value || typeof value !== 'object') return null;
  const root = value as Record<string, unknown>;
  const emscripten = root.emscripten as Record<string, unknown> | undefined;
  const files = root.files as Record<string, unknown> | undefined;

  if (
    !emscripten ||
    typeof emscripten.pinned !== 'string' ||
    typeof emscripten.installed !== 'string' ||
    emscripten.pinned !== emscripten.installed ||
    !files ||
    !ARTIFACT_KEYS.every((key) => isArtifactRecord(files[key]))
  ) {
    return null;
  }

  return value as WasmBuildManifest;
}

/**
 * Reports whether a complete, reproducible WASM build is present while Astro
 * renders the site. Invalid, stale, or partially written manifests degrade to
 * `ready: false`; they never make a normal Astro build fail.
 */
export function getWasmBuildStatus(rootDir = process.cwd()): WasmBuildStatus {
  try {
    const wasmDir = resolve(rootDir, 'public/wasm');
    const pin = readFileSync(resolve(rootDir, 'src/wasm/cpp/.emscripten-version'), 'utf8').trim();
    const rawManifest: unknown = JSON.parse(
      readFileSync(resolve(wasmDir, 'wasm-build.json'), 'utf8')
    );
    const manifest = parseManifest(rawManifest);

    if (!manifest || manifest.emscripten.pinned !== pin) {
      return { ready: false, manifest: null };
    }

    for (const key of ARTIFACT_KEYS) {
      const artifact = manifest.files[key];
      const stats = statSync(resolve(wasmDir, artifact.path));
      if (!stats.isFile() || stats.size <= 0 || stats.size !== artifact.bytes) {
        return { ready: false, manifest: null };
      }
    }

    return { ready: true, manifest };
  } catch {
    return { ready: false, manifest: null };
  }
}
