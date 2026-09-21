/**
 * Typed init failure reasons for the WASM audio bridge.
 * Used by RbsPlayer to show distinct degraded-mode messaging.
 */

export type InitFailureReason =
  | 'unsupported-browser'
  | 'not-cross-origin-isolated'
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
  'not-cross-origin-isolated':
    'Full playback needs cross-origin isolation (SharedArrayBuffer). Reload once if a service-worker update was pending, or open the site from its canonical URL.',
  'wasm-unavailable':
    'WASM engine binaries are not built yet — metadata preview and sketch playback are available.',
  'wasm-load-failed':
    'WASM engine failed to load (missing or blocked assets). Using degraded preview mode.',
  'worklet-unavailable':
    'AudioWorklet is not available in this browser. Metadata preview is still available.',
  'worklet-init-failed':
    'AudioWorklet initialisation failed. Using sketch preview instead of full synthesis.',
  'engine-init-failed': 'Audio engine initialisation failed. Using degraded preview mode.',
};

export function classifyInitError(err: unknown): InitFailure {
  if (err instanceof WasmInitError) {
    return err.failure;
  }

  const message = err instanceof Error ? err.message : String(err);
  const lower = message.toLowerCase();

  if (lower.includes('does not support required audio apis')) {
    if (typeof WebAssembly !== 'object') {
      return {
        reason: 'unsupported-browser',
        message: INIT_FAILURE_MESSAGES['unsupported-browser'],
        cause: err,
      };
    }
    if (typeof AudioWorkletNode === 'undefined') {
      return {
        reason: 'worklet-unavailable',
        message: INIT_FAILURE_MESSAGES['worklet-unavailable'],
        cause: err,
      };
    }
    return {
      reason: 'unsupported-browser',
      message: INIT_FAILURE_MESSAGES['unsupported-browser'],
      cause: err,
    };
  }

  if (
    lower.includes('failed to fetch') ||
    lower.includes('404') ||
    lower.includes('cannot find module') ||
    lower.includes('importing a module script failed') ||
    lower.includes('error loading dynamically imported module')
  ) {
    return {
      reason: 'wasm-load-failed',
      message: INIT_FAILURE_MESSAGES['wasm-load-failed'],
      cause: err,
    };
  }

  if (lower.includes('audioworklet')) {
    return {
      reason: 'worklet-init-failed',
      message: INIT_FAILURE_MESSAGES['worklet-init-failed'],
      cause: err,
    };
  }

  if (
    lower.includes('cross-origin isolation') ||
    lower.includes('crossoriginisolated') ||
    lower.includes('not-cross-origin-isolated')
  ) {
    return {
      reason: 'not-cross-origin-isolated',
      message: INIT_FAILURE_MESSAGES['not-cross-origin-isolated'],
      cause: err,
    };
  }

  return {
    reason: 'engine-init-failed',
    message: INIT_FAILURE_MESSAGES['engine-init-failed'],
    cause: err,
  };
}

// ── Catalog fetch failures ──────────────────────────────────────────
//
// The long-tail archive lives off-origin (ADR 0001). Under cross-origin
// isolation a remote file without `Cross-Origin-Resource-Policy:
// cross-origin` fails as an opaque network error — indistinguishable from
// "offline" at the `fetch` level, and nothing like a 404. The player has to
// say which one happened, because the remedies are different: a CORP block
// needs the file mirrored into the playable core, a 404 needs the catalog
// entry fixed, and a parse failure is a bug worth reporting.

export type LoadFailureReason =
  | 'corp-blocked'
  | 'offline'
  | 'not-found'
  | 'http-error'
  | 'parse'
  | 'unknown';

export interface LoadFailure {
  reason: LoadFailureReason;
  message: string;
  status?: number;
}

/** User-facing copy for each load failure, in the LCD's clipped register. */
export const LOAD_FAILURE_MESSAGES: Record<LoadFailureReason, string> = {
  'corp-blocked':
    'Blocked by cross-origin isolation — this file is not mirrored into the playable core yet. Download it and drop it on the player.',
  offline: 'Network unavailable — the file could not be fetched.',
  'not-found': 'Not in the archive (404) — the catalog entry points at a missing file.',
  'http-error': 'The server refused the file.',
  parse: 'The file downloaded but is not a readable .rbs.',
  unknown: 'Preview fetch failed — try loading a local copy.',
};

/**
 * An HTTP error carrying its status, so a failed response can be classified
 * with the same call as a thrown network error.
 */
export class RbsFetchError extends Error {
  readonly status: number;
  readonly url: string;

  constructor(status: number, url: string) {
    super(`HTTP ${status}`);
    this.name = 'RbsFetchError';
    this.status = status;
    this.url = url;
  }
}

/** Thrown when the bytes arrived but the parser rejected them. */
export class RbsParseError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'RbsParseError';
  }
}

/**
 * Tell a CORP block apart from an ordinary offline failure.
 *
 * `fetch` gives a bare `TypeError` for both, so the only usable signal is
 * whether the request left this origin at all while the page is
 * cross-origin isolated.
 */
export function classifyLoadError(err: unknown, url?: string): LoadFailure {
  if (err instanceof RbsParseError) {
    return { reason: 'parse', message: LOAD_FAILURE_MESSAGES.parse };
  }
  if (err instanceof RbsFetchError) {
    const reason: LoadFailureReason = err.status === 404 ? 'not-found' : 'http-error';
    return {
      reason,
      message:
        reason === 'not-found'
          ? LOAD_FAILURE_MESSAGES['not-found']
          : `${LOAD_FAILURE_MESSAGES['http-error']} (HTTP ${err.status})`,
      status: err.status,
    };
  }
  if (err instanceof TypeError) {
    if (url && isCrossOrigin(url) && isIsolated()) {
      return { reason: 'corp-blocked', message: LOAD_FAILURE_MESSAGES['corp-blocked'] };
    }
    return { reason: 'offline', message: LOAD_FAILURE_MESSAGES.offline };
  }
  return { reason: 'unknown', message: LOAD_FAILURE_MESSAGES.unknown };
}

/** True when `url` resolves to somewhere other than this page's origin. */
export function isCrossOrigin(url: string): boolean {
  if (typeof window === 'undefined') return false;
  try {
    return new URL(url, window.location.href).origin !== window.location.origin;
  } catch {
    return false;
  }
}

function isIsolated(): boolean {
  return typeof globalThis !== 'undefined' && Boolean(globalThis.crossOriginIsolated);
}
