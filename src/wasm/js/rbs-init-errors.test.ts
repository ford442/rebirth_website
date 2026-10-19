import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  INIT_FAILURE_MESSAGES,
  LOAD_FAILURE_MESSAGES,
  RbsFetchError,
  RbsParseError,
  WasmInitError,
  asParseError,
  classifyInitError,
  classifyLoadError,
  errorMessage,
  isCrossOrigin,
  type InitFailureReason,
} from './rbs-init-errors';

afterEach(() => vi.unstubAllGlobals());

describe('classifyInitError', () => {
  /**
   * The matcher is a first-hit-wins chain over the lower-cased message, so the
   * order of rows is part of the contract: a message that mentions both a 404
   * and the AudioWorklet is a load failure, not a worklet failure.
   */
  const table: Array<[string, string, InitFailureReason]> = [
    ['failed fetch', 'TypeError: Failed to fetch', 'wasm-load-failed'],
    ['a 404', 'GET /wasm/rbsParser.js 404', 'wasm-load-failed'],
    ['a missing module', "Cannot find module '/wasm/rbsParser.js'", 'wasm-load-failed'],
    ['a failed module import', 'Importing a module script failed.', 'wasm-load-failed'],
    [
      'a failed dynamic import',
      'error loading dynamically imported module: /wasm/x.js',
      'wasm-load-failed',
    ],
    ['an AudioWorklet failure', 'AudioWorklet node was not created', 'worklet-init-failed'],
    ['a lower-case audioworklet', 'audioworklet addModule rejected', 'worklet-init-failed'],
    [
      'isolation, spelled out',
      'SharedArrayBuffer needs cross-origin isolation',
      'not-cross-origin-isolated',
    ],
    ['isolation, as the global', 'crossOriginIsolated is false', 'not-cross-origin-isolated'],
    ['isolation, as the reason code', 'not-cross-origin-isolated', 'not-cross-origin-isolated'],
    ['an unrecognised error', 'something exploded', 'engine-init-failed'],
    ['an empty message', '', 'engine-init-failed'],
    ['a 404 in an AudioWorklet message (404 wins)', 'AudioWorklet module 404', 'wasm-load-failed'],
    [
      'a fetch failure that mentions isolation (fetch wins)',
      'Failed to fetch under cross-origin isolation',
      'wasm-load-failed',
    ],
  ];

  it.each(table)('%s → %s', (_name, message, reason) => {
    const failure = classifyInitError(new Error(message));
    expect(failure.reason).toBe(reason);
    expect(failure.message).toBe(INIT_FAILURE_MESSAGES[reason]);
  });

  it('is case-insensitive', () => {
    expect(classifyInitError(new Error('FAILED TO FETCH')).reason).toBe('wasm-load-failed');
  });

  it('keeps the original error as the cause', () => {
    const err = new Error('something exploded');
    expect(classifyInitError(err).cause).toBe(err);
  });

  it('classifies a thrown non-Error by its string form', () => {
    expect(classifyInitError('Failed to fetch').reason).toBe('wasm-load-failed');
    expect(classifyInitError(404).reason).toBe('wasm-load-failed');
    expect(classifyInitError(undefined).reason).toBe('engine-init-failed');
    expect(classifyInitError(null).reason).toBe('engine-init-failed');
  });

  it('hands a WasmInitError back untouched, whatever its message says', () => {
    const failure = {
      reason: 'wasm-unavailable' as const,
      message: 'custom copy with a 404 and audioworklet in it',
    };
    expect(classifyInitError(new WasmInitError(failure))).toBe(failure);
  });

  describe('the "does not support required audio apis" branch', () => {
    const err = new Error('Browser does not support required audio APIs');

    it('is a missing WebAssembly when WebAssembly is gone', () => {
      vi.stubGlobal('WebAssembly', undefined);
      expect(classifyInitError(err).reason).toBe('unsupported-browser');
    });

    it('is a missing AudioWorklet when only that is gone', () => {
      vi.stubGlobal('AudioWorkletNode', undefined);
      expect(classifyInitError(err).reason).toBe('worklet-unavailable');
    });

    it('is an unsupported browser when both APIs exist but the engine still refused', () => {
      vi.stubGlobal('AudioWorkletNode', class {});
      expect(classifyInitError(err).reason).toBe('unsupported-browser');
    });
  });

  it('has user-facing copy for every reason', () => {
    const reasons: InitFailureReason[] = [
      'unsupported-browser',
      'not-cross-origin-isolated',
      'wasm-unavailable',
      'wasm-load-failed',
      'worklet-unavailable',
      'worklet-init-failed',
      'engine-init-failed',
    ];
    for (const reason of reasons) {
      expect(INIT_FAILURE_MESSAGES[reason].length).toBeGreaterThan(20);
    }
    expect(Object.keys(INIT_FAILURE_MESSAGES).sort()).toEqual([...reasons].sort());
  });
});

describe('WasmInitError', () => {
  it('carries its failure and reads like an Error', () => {
    const failure = { reason: 'wasm-unavailable' as const, message: 'not built' };
    const err = new WasmInitError(failure);
    expect(err).toBeInstanceOf(Error);
    expect(err.name).toBe('WasmInitError');
    expect(err.message).toBe('not built');
    expect(err.failure).toBe(failure);
  });
});

describe('classifyLoadError', () => {
  it('tells a 404 from other HTTP errors', () => {
    const notFound = classifyLoadError(new RbsFetchError(404, 'https://x.test/a.rbs'));
    expect(notFound).toEqual({
      reason: 'not-found',
      message: LOAD_FAILURE_MESSAGES['not-found'],
      status: 404,
    });

    for (const status of [400, 403, 500, 503]) {
      const failure = classifyLoadError(new RbsFetchError(status, 'https://x.test/a.rbs'));
      expect(failure.reason).toBe('http-error');
      expect(failure.status).toBe(status);
      expect(failure.message).toContain(`HTTP ${status}`);
    }
  });

  describe('a network TypeError', () => {
    const failed = new TypeError('Failed to fetch');

    it('is offline for a same-origin file', () => {
      vi.stubGlobal('window', {
        location: { href: 'https://site.test/rebirth_website/play', origin: 'https://site.test' },
      });
      vi.stubGlobal('crossOriginIsolated', true);
      expect(classifyLoadError(failed, '/rebirth_website/a.rbs').reason).toBe('offline');
      expect(classifyLoadError(failed, 'https://site.test/x/a.rbs').reason).toBe('offline');
    });

    it('is offline when no URL is known', () => {
      expect(classifyLoadError(failed).reason).toBe('offline');
    });

    it('is a CORP block for an off-origin file on an isolated page', () => {
      vi.stubGlobal('window', {
        location: { href: 'https://site.test/rebirth_website/play', origin: 'https://site.test' },
      });
      vi.stubGlobal('crossOriginIsolated', true);
      const failure = classifyLoadError(failed, 'https://test.1ink.us/rb338/a.rbs');
      expect(failure).toEqual({
        reason: 'corp-blocked',
        message: LOAD_FAILURE_MESSAGES['corp-blocked'],
      });
    });

    it('is just offline for the same off-origin file when the page is not isolated', () => {
      vi.stubGlobal('window', {
        location: { href: 'https://site.test/rebirth_website/play', origin: 'https://site.test' },
      });
      vi.stubGlobal('crossOriginIsolated', false);
      expect(classifyLoadError(failed, 'https://test.1ink.us/rb338/a.rbs').reason).toBe('offline');
    });
  });

  it('is a parse failure for an RbsParseError, keeping the engine code', () => {
    const failure = classifyLoadError(new RbsParseError('Bad magic number', 'PARSE_ERROR'));
    expect(failure).toEqual({
      reason: 'parse',
      message: 'Could not load this .rbs: Bad magic number',
      code: 'PARSE_ERROR',
    });
  });

  it('falls back to the generic parse copy when the error says nothing', () => {
    expect(classifyLoadError(new RbsParseError('')).message).toBe(LOAD_FAILURE_MESSAGES.parse);
    expect(classifyLoadError(new RbsParseError('   ')).message).toBe(LOAD_FAILURE_MESSAGES.parse);
  });

  it('caps a runaway engine message', () => {
    const failure = classifyLoadError(new RbsParseError('x'.repeat(5000)));
    expect(failure.message.length).toBeLessThan(220);
  });

  it('is unknown for anything else', () => {
    expect(classifyLoadError(new Error('who knows'))).toEqual({
      reason: 'unknown',
      message: LOAD_FAILURE_MESSAGES.unknown,
    });
    expect(classifyLoadError('a string').reason).toBe('unknown');
    expect(classifyLoadError(undefined).reason).toBe('unknown');
  });
});

describe('engine errors reach the user (not "[object Object]")', () => {
  // `WasmAudioBridge` rejects with plain `{ code, message }` objects.
  const heapBudget = {
    code: 'PARSE_ERROR',
    message: 'Not enough WASM heap for a 900 KB song.',
  };

  it('errorMessage reads Errors, strings and {message} objects', () => {
    expect(errorMessage(new Error('boom'))).toBe('boom');
    expect(errorMessage('plain')).toBe('plain');
    expect(errorMessage(heapBudget)).toBe(heapBudget.message);
    expect(errorMessage({ message: 42 })).toBe('');
    expect(errorMessage({})).toBe('');
    expect(errorMessage(null)).toBe('');
    expect(errorMessage(undefined)).toBe('');
  });

  it('asParseError keeps the message and code of an engine error', () => {
    const wrapped = asParseError(heapBudget);
    expect(wrapped).toBeInstanceOf(RbsParseError);
    expect(wrapped.message).toBe(heapBudget.message);
    expect(wrapped.code).toBe('PARSE_ERROR');
  });

  it('asParseError returns an RbsParseError as it is, and copes with non-errors', () => {
    const parse = new RbsParseError('bad chunk');
    expect(asParseError(parse)).toBe(parse);
    expect(asParseError(new Error('boom')).message).toBe('boom');
    expect(asParseError(undefined).message).toBe('');
    expect(asParseError({ code: 7, message: 'x' }).code).toBeUndefined();
  });

  it('puts the heap-budget sentence on the LCD', () => {
    const failure = classifyLoadError(asParseError(heapBudget));
    expect(failure.reason).toBe('parse');
    expect(failure.message).toContain('Not enough WASM heap for a 900 KB song.');
    expect(failure.message).not.toContain('[object Object]');
    expect(failure.code).toBe('PARSE_ERROR');
  });

  it('String(err) is what used to be shown', () => {
    expect(String(heapBudget)).toBe('[object Object]');
  });
});

describe('isCrossOrigin', () => {
  it('is false outside a page', () => {
    expect(isCrossOrigin('https://elsewhere.test/a.rbs')).toBe(false);
  });

  it('compares against the page origin, resolving relative URLs', () => {
    vi.stubGlobal('window', {
      location: { href: 'https://site.test/rebirth_website/play', origin: 'https://site.test' },
    });
    expect(isCrossOrigin('/archive/a.rbs')).toBe(false);
    expect(isCrossOrigin('a.rbs')).toBe(false);
    expect(isCrossOrigin('https://site.test/a.rbs')).toBe(false);
    expect(isCrossOrigin('https://other.test/a.rbs')).toBe(true);
    expect(isCrossOrigin('http://site.test/a.rbs')).toBe(true); // scheme differs
    expect(isCrossOrigin('https://site.test:8443/a.rbs')).toBe(true); // port differs
  });

  it('treats an unparseable URL as same-origin rather than throwing', () => {
    vi.stubGlobal('window', {
      location: { href: 'https://site.test/', origin: 'https://site.test' },
    });
    expect(isCrossOrigin('http://[bad')).toBe(false);
  });
});
