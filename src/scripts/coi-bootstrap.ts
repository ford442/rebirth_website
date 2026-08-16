/**
 * Wait for cross-origin isolation when the COI service worker is still installing.
 * Reloads once when the SW is active but not yet controlling the page.
 */
export async function waitForCrossOriginIsolation(timeoutMs = 10000): Promise<boolean> {
  if (typeof window === 'undefined') return false;
  if (typeof crossOriginIsolated !== 'undefined' && crossOriginIsolated) {
    return true;
  }

  const nav = navigator;
  if (!nav.serviceWorker) {
    return false;
  }

  const reloadedFlag = sessionStorage.getItem('coiReloadedBySelf');
  sessionStorage.removeItem('coiReloadedBySelf');

  try {
    const registration = await nav.serviceWorker.ready;
    if (registration.active && !nav.serviceWorker.controller && !reloadedFlag) {
      sessionStorage.setItem('coiReloadedBySelf', 'notcontrolling');
      window.location.reload();
      await new Promise<void>(() => {
        /* page reload in progress */
      });
    }
  } catch {
    return false;
  }

  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (crossOriginIsolated) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }

  return crossOriginIsolated;
}
