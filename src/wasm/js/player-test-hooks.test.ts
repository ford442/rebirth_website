import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  TEST_HOOKS_STORAGE_KEY,
  installPlayerTestHooks,
  testHooksEnabled,
  type PlayerTestHooks,
} from './player-test-hooks';

const HOOK_NAMES: Array<keyof PlayerTestHooks> = [
  'WasmAudioBridge',
  'DegradedRbsPlayer',
  'createProductionAudioContext',
  'attachAudioContextLifecycle',
  'resolveAudioContextConstructor',
  'songToMidi',
];

/** A `window` as production serves it: no dev server, optional query and storage. */
function stubPage(
  options: { search?: string; stored?: string | null; storageThrows?: boolean } = {}
) {
  vi.stubEnv('DEV', false);
  const storage = {
    getItem: vi.fn((key: string) => {
      if (options.storageThrows) throw new DOMException('blocked', 'SecurityError');
      return key === TEST_HOOKS_STORAGE_KEY ? (options.stored ?? null) : null;
    }),
  };
  const page: Record<string, unknown> = {
    location: { search: options.search ?? '' },
    localStorage: storage,
  };
  vi.stubGlobal('window', page);
  return page;
}

afterEach(() => {
  vi.unstubAllGlobals();
  vi.unstubAllEnvs();
});

describe('testHooksEnabled', () => {
  it('is on under the dev server', () => {
    vi.stubEnv('DEV', true);
    expect(testHooksEnabled()).toBe(true);
  });

  it('is off for an ordinary production visitor', () => {
    stubPage();
    expect(testHooksEnabled()).toBe(false);
  });

  it('is off outside a page', () => {
    vi.stubEnv('DEV', false);
    expect(testHooksEnabled()).toBe(false);
  });

  it.each([['?test'], ['?test=1'], ['?play=a.rbs&test'], ['?test=']])(
    'turns on for %s',
    (search) => {
      stubPage({ search });
      expect(testHooksEnabled()).toBe(true);
    }
  );

  it.each([['?testing'], ['?contest=1'], ['?play=test.rbs'], ['?p=test']])(
    'ignores %s — only a parameter called "test" counts',
    (search) => {
      stubPage({ search });
      expect(testHooksEnabled()).toBe(false);
    }
  );

  it('turns on from localStorage, which is how the Playwright config opts in', () => {
    stubPage({ stored: '1' });
    expect(testHooksEnabled()).toBe(true);
  });

  it.each([['0'], ['true'], ['yes'], [''], [null]])('ignores a stored value of %j', (stored) => {
    stubPage({ stored });
    expect(testHooksEnabled()).toBe(false);
  });

  it('stays off when storage throws (private windows, blocked site data)', () => {
    stubPage({ storageThrows: true });
    expect(testHooksEnabled()).toBe(false);
  });

  it('still honours ?test when storage throws', () => {
    stubPage({ search: '?test', storageThrows: true });
    expect(testHooksEnabled()).toBe(true);
  });
});

describe('installPlayerTestHooks', () => {
  it('puts nothing on window in production unless asked', () => {
    const page = stubPage();
    expect(installPlayerTestHooks()).toBe(false);
    for (const name of HOOK_NAMES) expect(page).not.toHaveProperty(name);
  });

  it('installs every hook when asked, and only those', () => {
    const page = stubPage({ search: '?test' });
    const before = new Set(Object.keys(page));

    expect(installPlayerTestHooks()).toBe(true);

    for (const name of HOOK_NAMES) expect(page[name]).toBeDefined();
    const added = Object.keys(page).filter((key) => !before.has(key));
    expect(added.sort()).toEqual([...HOOK_NAMES].sort());
  });

  it('does not expose the pure helpers that Vitest covers directly', () => {
    const page = stubPage({ search: '?test' });
    installPlayerTestHooks();
    for (const name of [
      'mapMidiMessage',
      'encodeStudioPatch',
      'decodeStudioPatch',
      'buildShareUrl',
      'classifyLoadError',
      'RbsFetchError',
      'RbsParseError',
    ]) {
      expect(page).not.toHaveProperty(name);
    }
  });

  it('is idempotent', () => {
    const page = stubPage({ stored: '1' });
    installPlayerTestHooks();
    const first = page.WasmAudioBridge;
    installPlayerTestHooks();
    expect(page.WasmAudioBridge).toBe(first);
  });
});
