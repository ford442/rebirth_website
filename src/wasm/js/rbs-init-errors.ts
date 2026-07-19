/**
 * Typed init failure reasons for the WASM audio bridge.
 * Used by RbsPlayer to show distinct degraded-mode messaging.
 */

export type InitFailureReason =
  | 'unsupported-browser'
  | 'wasm-unavailable'
  | 'wasm-load-failed'
  | 'worklet-unavailable'
  | 'worklet-init-failed'
  | 'engine-init-failed';

export interface InitFailure {
  reason: InitFailureReason;
  message: string;
  cause?: unknown;
}

export class WasmInitError extends Error {
  readonly failure: InitFailure;

  constructor(failure: InitFailure) {
    super(failure.message);
    this.name = 'WasmInitError';
    this.failure = failure;
  }
}

/** User-facing copy for each failure reason. */
export const INIT_FAILURE_MESSAGES: Record<InitFailureReason, string> = {
  'unsupported-browser':
    'This browser lacks WebAssembly or AudioWorklet support required for full playback.',
  'wasm-unavailable':
    'WASM engine binaries are not built yet — metadata preview and sketch playback are available.',
  'wasm-load-failed':
    'WASM engine failed to load (missing or blocked assets). Using degraded preview mode.',
  'worklet-unavailable':
    'AudioWorklet is not available in this browser. Metadata preview is still available.',
  'worklet-init-failed':
    'AudioWorklet initialisation failed. Using sketch preview instead of full synthesis.',
  'engine-init-failed':
    'Audio engine initialisation failed. Using degraded preview mode.',
};

export function classifyInitError(err: unknown): InitFailure {
  if (err instanceof WasmInitError) {
    return err.failure;
  }

  const message = err instanceof Error ? err.message : String(err);
  const lower = message.toLowerCase();

  if (lower.includes('does not support required audio apis')) {
    if (typeof WebAssembly !== 'object') {
      return { reason: 'unsupported-browser', message: INIT_FAILURE_MESSAGES['unsupported-browser'], cause: err };
    }
    if (typeof AudioWorkletNode === 'undefined') {
      return { reason: 'worklet-unavailable', message: INIT_FAILURE_MESSAGES['worklet-unavailable'], cause: err };
    }
    return { reason: 'unsupported-browser', message: INIT_FAILURE_MESSAGES['unsupported-browser'], cause: err };
  }

  if (
    lower.includes('failed to fetch') ||
    lower.includes('404') ||
    lower.includes('cannot find module') ||
    lower.includes('importing a module script failed') ||
    lower.includes('error loading dynamically imported module')
  ) {
    return { reason: 'wasm-load-failed', message: INIT_FAILURE_MESSAGES['wasm-load-failed'], cause: err };
  }

  if (lower.includes('audioworklet')) {
    return { reason: 'worklet-init-failed', message: INIT_FAILURE_MESSAGES['worklet-init-failed'], cause: err };
  }

  return { reason: 'engine-init-failed', message: INIT_FAILURE_MESSAGES['engine-init-failed'], cause: err };
}
