/**
 * Map Emscripten locateFile() requests onto the shipping public/wasm paths.
 *
 * Must not mutate generated glue. AudioWorkletGlobalScope requests the glue
 * basename (`rbsParser.js`); we remap those to the checked-in bootstrap so
 * the worklet imports glue for side effects only.
 */

export interface WasmLocatePaths {
  wasmPath: string;
  glueScriptPath: string;
  workletPath: string;
  pthreadWorkerPath?: string;
}

function inAudioWorkletGlobalScope(): boolean {
  return (
    typeof (globalThis as { AudioWorkletGlobalScope?: unknown }).AudioWorkletGlobalScope !==
    'undefined'
  );
}

export function locateWasmAsset(path: string, paths: WasmLocatePaths): string {
  if (path.endsWith('.wasm')) return paths.wasmPath;
  if (path.endsWith('.ww.js')) {
    return paths.pthreadWorkerPath ?? path;
  }
  if (path.endsWith('rbsWorklet.js') || (path.endsWith('.js') && path.includes('.aw.'))) {
    return paths.workletPath;
  }
  if (path.endsWith('rbsParser.js') && inAudioWorkletGlobalScope()) {
    return paths.workletPath;
  }
  if (path.endsWith('rbsParser.js')) {
    return paths.glueScriptPath;
  }
  return path;
}
