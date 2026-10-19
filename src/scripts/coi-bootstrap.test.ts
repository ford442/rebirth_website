import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { waitForCrossOriginIsolation } from './coi-bootstrap';

/** Settles to the resolved value, or stays `'pending'`. */
function track<T>(promise: Promise<T>) {
  const state: { value: T | 'pending' } = { value: 'pending' };
  void promise.then((value) => {
    state.value = value;
  });
  return state;
}

function memoryStorage(initial: Record<string, string> = {}) {
  const items = new Map(Object.entries(initial));
  return {
    items,
    getItem: (key: string) => items.get(key) ?? null,
    setItem: (key: string, value: string) => void items.set(key, String(value)),
    removeItem: (key: string) => void items.delete(key),
  };
}

type StorageLike = Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>;

const throwingStorage = (): StorageLike => {
  const fail = () => {
    throw new DOMException('The operation is insecure.', 'SecurityError');
  };
  return { getItem: fail, setItem: fail, removeItem: fail };
};

interface Env {
  isolated: boolean;
  reload: ReturnType<typeof vi.fn>;
  storage: StorageLike;
}

/** Stub the browser globals the bootstrap reads. */
function setup(
  options: {
    ready?: Promise<unknown>;
    controller?: unknown;
    storage?: Env['storage'];
    serviceWorker?: boolean;
  } = {}
): Env {
  const env: Env = {
    isolated: false,
    reload: vi.fn(),
    storage: options.storage ?? memoryStorage(),
  };
  vi.stubGlobal('window', { location: { reload: env.reload } });
  vi.stubGlobal('navigator', {
    serviceWorker:
      options.serviceWorker === false
        ? undefined
        : {
            ready: options.ready ?? Promise.resolve({ active: {} }),
            controller: options.controller ?? null,
          },
  });
  vi.stubGlobal('sessionStorage', env.storage);
  Object.defineProperty(globalThis, 'crossOriginIsolated', {
    configurable: true,
    get: () => env.isolated,
  });
  return env;
}

beforeEach(() => vi.useFakeTimers());
afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
  Reflect.deleteProperty(globalThis, 'crossOriginIsolated');
});

describe('waitForCrossOriginIsolation', () => {
  it('is false outside a page', async () => {
    // No `window` stub: this is plain Node.
    expect(await waitForCrossOriginIsolation(50)).toBe(false);
  });

  it('returns true straight away when the page is already isolated', async () => {
    const env = setup();
    env.isolated = true;
    expect(await waitForCrossOriginIsolation()).toBe(true);
    expect(env.reload).not.toHaveBeenCalled();
  });

  it('is false at once when the browser has no service workers', async () => {
    setup({ serviceWorker: false });
    expect(await waitForCrossOriginIsolation()).toBe(false);
  });

  it('resolves true as soon as the page becomes isolated', async () => {
    const env = setup({ controller: {} });
    const result = track(waitForCrossOriginIsolation(10_000));

    await vi.advanceTimersByTimeAsync(250);
    expect(result.value).toBe('pending');

    env.isolated = true;
    await vi.advanceTimersByTimeAsync(100);
    expect(result.value).toBe(true);
  });

  it('gives up after the timeout when isolation never arrives', async () => {
    setup({ controller: {} });
    const result = track(waitForCrossOriginIsolation(1_000));

    await vi.advanceTimersByTimeAsync(900);
    expect(result.value).toBe('pending');
    await vi.advanceTimersByTimeAsync(200);
    expect(result.value).toBe(false);
  });

  describe('when the service worker never registers (Firefox private mode)', () => {
    // `serviceWorker.ready` simply never settles. It used to be awaited with
    // no bound, so init hung forever and the timeout never started.
    it('still gives up after the timeout instead of hanging', async () => {
      const env = setup({ ready: new Promise(() => {}) });
      const result = track(waitForCrossOriginIsolation(10_000));

      await vi.advanceTimersByTimeAsync(9_900);
      expect(result.value).toBe('pending');
      await vi.advanceTimersByTimeAsync(200);
      expect(result.value).toBe(false);
      expect(env.reload).not.toHaveBeenCalled();
    });

    it('counts that wait against the same deadline as the isolation poll', async () => {
      const env = setup({
        ready: new Promise((resolve) => setTimeout(() => resolve({ active: {} }), 4_000)),
        controller: {},
      });
      const result = track(waitForCrossOriginIsolation(10_000));

      await vi.advanceTimersByTimeAsync(9_900);
      expect(result.value).toBe('pending');
      await vi.advanceTimersByTimeAsync(200);
      // 10 s in total, not 4 s of waiting for the worker plus another 10 s.
      expect(result.value).toBe(false);

      env.isolated = false;
    });

    it('is not fooled by isolation arriving late', async () => {
      const env = setup({
        ready: new Promise((resolve) => setTimeout(() => resolve({ active: {} }), 2_000)),
        controller: {},
      });
      const result = track(waitForCrossOriginIsolation(10_000));
      await vi.advanceTimersByTimeAsync(2_500);
      env.isolated = true;
      await vi.advanceTimersByTimeAsync(150);
      expect(result.value).toBe(true);
    });
  });

  it('is false when serviceWorker.ready rejects', async () => {
    setup({ ready: Promise.reject(new Error('no registration')) });
    expect(await waitForCrossOriginIsolation(1_000)).toBe(false);
  });

  describe('the one-time reload for a worker that is active but not yet in control', () => {
    it('reloads once and remembers that it did', async () => {
      const storage = memoryStorage();
      const env = setup({ storage });
      const result = track(waitForCrossOriginIsolation(10_000));
      await vi.advanceTimersByTimeAsync(0);

      expect(env.reload).toHaveBeenCalledTimes(1);
      expect(storage.items.get('coiReloadedBySelf')).toBe('notcontrolling');
      // The page is going away; the promise deliberately never settles.
      await vi.advanceTimersByTimeAsync(60_000);
      expect(result.value).toBe('pending');
    });

    it('does not reload a second time after coming back from the first', async () => {
      const storage = memoryStorage({ coiReloadedBySelf: 'notcontrolling' });
      const env = setup({ storage });
      const result = track(waitForCrossOriginIsolation(500));

      await vi.advanceTimersByTimeAsync(600);
      expect(env.reload).not.toHaveBeenCalled();
      expect(result.value).toBe(false);
      // The flag is spent, so a later visit may reload again.
      expect(storage.items.has('coiReloadedBySelf')).toBe(false);
    });

    it('does not reload when the worker already controls the page', async () => {
      const env = setup({ controller: {} });
      const result = track(waitForCrossOriginIsolation(300));
      await vi.advanceTimersByTimeAsync(400);
      expect(env.reload).not.toHaveBeenCalled();
      expect(result.value).toBe(false);
    });

    it('does not reload while the worker is still installing (not active)', async () => {
      const env = setup({ ready: Promise.resolve({ active: null }) });
      const result = track(waitForCrossOriginIsolation(300));
      await vi.advanceTimersByTimeAsync(400);
      expect(env.reload).not.toHaveBeenCalled();
      expect(result.value).toBe(false);
    });
  });

  describe('when sessionStorage throws (private windows, blocked site data)', () => {
    it('does not throw out of the bootstrap', async () => {
      setup({ storage: throwingStorage(), controller: {} });
      const result = track(waitForCrossOriginIsolation(300));
      await vi.advanceTimersByTimeAsync(400);
      expect(result.value).toBe(false);
    });

    it('does not reload — it could not remember having done so, so it would loop', async () => {
      const env = setup({ storage: throwingStorage() });
      const result = track(waitForCrossOriginIsolation(300));
      await vi.advanceTimersByTimeAsync(400);
      expect(env.reload).not.toHaveBeenCalled();
      expect(result.value).toBe(false);
    });
  });

  it('does not reload when the flag cannot be read back after being set', async () => {
    // Storage that accepts writes and forgets them (some embedded webviews).
    const amnesiac = {
      getItem: () => null,
      setItem: () => {},
      removeItem: () => {},
    };
    const env = setup({ storage: amnesiac });
    const result = track(waitForCrossOriginIsolation(300));
    await vi.advanceTimersByTimeAsync(400);
    expect(env.reload).not.toHaveBeenCalled();
    expect(result.value).toBe(false);
  });
});
