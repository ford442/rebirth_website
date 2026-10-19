const RELOAD_FLAG = 'coiReloadedBySelf';

/**
 * Read and clear the "we already reloaded once" flag.
 *
 * `sessionStorage` throws in some private windows and with blocked site data,
 * and that must not take the bridge's init down with it.
 */
function takeReloadFlag(): boolean {
  try {
    const value = sessionStorage.getItem(RELOAD_FLAG);
    sessionStorage.removeItem(RELOAD_FLAG);
    return value !== null;
  } catch {
    return false;
  }
}

/**
 * Remember that we are about to reload. Returns false when the flag could not
 * be persisted — the caller must then not reload, or the page would reload
 * forever with nothing to say it already tried.
 */
function setReloadFlag(): boolean {
  try {
    sessionStorage.setItem(RELOAD_FLAG, 'notcontrolling');
    return sessionStorage.getItem(RELOAD_FLAG) !== null;
  } catch {
    return false;
  }
}

/** Resolve with `promise`'s value, or `undefined` once `ms` have passed. */
function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T | undefined> {
  return new Promise<T | undefined>((resolve, reject) => {
    const timer = setTimeout(() => resolve(undefined), Math.max(0, ms));
    promise.then(
      (value) => {
        clearTimeout(timer);
        resolve(value);
      },
      (err) => {
        clearTimeout(timer);
        reject(err);
      }
    );
  });
}

function isIsolated(): boolean {
  return Boolean(globalThis.crossOriginIsolated);
}

/**
 * Wait for cross-origin isolation when the COI service worker is still installing.
 * Reloads once when the SW is active but not yet controlling the page.
 *
 * `timeoutMs` bounds the whole wait. `serviceWorker.ready` never settles when
 * the worker failed to register (Firefox private mode, blocked scripts), so it
 * is raced against the same deadline as the isolation poll.
 */
export async function waitForCrossOriginIsolation(timeoutMs = 10000): Promise<boolean> {
  if (typeof window === 'undefined') return false;
  if (isIsolated()) {
    return true;
  }

  const nav = navigator;
  if (!nav.serviceWorker) {
    return false;
  }

  const deadline = Date.now() + timeoutMs;
  const alreadyReloaded = takeReloadFlag();

  try {
    const registration = await withTimeout(nav.serviceWorker.ready, timeoutMs);
    if (
      registration?.active &&
      !nav.serviceWorker.controller &&
      !alreadyReloaded &&
      setReloadFlag()
    ) {
      window.location.reload();
      await new Promise<void>(() => {
        /* page reload in progress */
      });
    }
  } catch {
    return false;
  }

  while (Date.now() < deadline) {
    if (isIsolated()) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }

  return isIsolated();
}
