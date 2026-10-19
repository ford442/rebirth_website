import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  attachAudioContextLifecycle,
  contextNeedsUserGesture,
  createProductionAudioContext,
  formatAudioContextDiagnostics,
  resolveAudioContextConstructor,
} from './create-audio-context';

afterEach(() => vi.unstubAllGlobals());

type CtorOptions = { sampleRate?: number; latencyHint?: string } | undefined;

/** An AudioContext stand-in that records how it was constructed. */
function fakeCtor(
  behaviour: (options: CtorOptions, call: number) => void = () => {},
  props: { sampleRate?: number; baseLatency?: unknown; outputLatency?: unknown } = {}
) {
  const calls: CtorOptions[] = [];
  class FakeAudioContext {
    sampleRate: number;
    baseLatency: unknown;
    outputLatency: unknown;
    constructor(options?: CtorOptions) {
      calls.push(options);
      behaviour(options, calls.length);
      this.sampleRate = props.sampleRate ?? options?.sampleRate ?? 44100;
      this.baseLatency = 'baseLatency' in props ? props.baseLatency : 0.01;
      this.outputLatency = 'outputLatency' in props ? props.outputLatency : 0.02;
    }
  }
  return { Ctor: FakeAudioContext, calls };
}

const notSupported = () =>
  new DOMException('The sample rate is not supported', 'NotSupportedError');

describe('createProductionAudioContext', () => {
  it('asks for the preferred rate and the interactive hint first', () => {
    const { Ctor, calls } = fakeCtor();
    vi.stubGlobal('AudioContext', Ctor);

    const { diagnostics } = createProductionAudioContext(48000);

    expect(calls).toEqual([{ sampleRate: 48000, latencyHint: 'interactive' }]);
    expect(diagnostics).toEqual({
      sampleRate: 48000,
      latencyHint: 'interactive',
      baseLatency: 0.01,
      outputLatency: 0.02,
      requestedSampleRate: 48000,
    });
  });

  it('passes a custom latency hint through', () => {
    const { Ctor, calls } = fakeCtor();
    vi.stubGlobal('AudioContext', Ctor);
    createProductionAudioContext(44100, 'playback');
    expect(calls[0]).toEqual({ sampleRate: 44100, latencyHint: 'playback' });
  });

  describe('when the browser rejects the requested sample rate (NotSupportedError)', () => {
    it('retries without sampleRate and says so in the diagnostics', () => {
      const { Ctor, calls } = fakeCtor(
        (options) => {
          if (options?.sampleRate !== undefined) throw notSupported();
        },
        { sampleRate: 44100 }
      );
      vi.stubGlobal('AudioContext', Ctor);

      const { context, diagnostics } = createProductionAudioContext(48000);

      expect(calls).toEqual([
        { sampleRate: 48000, latencyHint: 'interactive' },
        { latencyHint: 'interactive' },
      ]);
      expect(context).toBeInstanceOf(Ctor);
      expect(diagnostics.sampleRate).toBe(44100);
      expect(diagnostics.requestedSampleRate).toBeNull();
      expect(diagnostics.latencyHint).toBe('interactive');
    });

    it("keeps the caller's latency hint on the retry", () => {
      const { Ctor, calls } = fakeCtor((options) => {
        if (options?.sampleRate !== undefined) throw notSupported();
      });
      vi.stubGlobal('AudioContext', Ctor);
      createProductionAudioContext(96000, 'balanced');
      expect(calls[1]).toEqual({ latencyHint: 'balanced' });
    });

    it('lets a second NotSupportedError through — there is nothing left to relax', () => {
      const { Ctor, calls } = fakeCtor(() => {
        throw notSupported();
      });
      vi.stubGlobal('AudioContext', Ctor);

      expect(() => createProductionAudioContext(48000)).toThrow(/sample rate/);
      expect(calls).toHaveLength(2);
    });
  });

  describe('other failures are not retried', () => {
    it.each([
      ['a different DOMException', new DOMException('blocked', 'SecurityError')],
      ['an InvalidAccessError', new DOMException('bad', 'InvalidAccessError')],
      ['a TypeError', new TypeError('bad option')],
      ['a plain Error', new Error('boom')],
    ])('%s', (_name, error) => {
      const { Ctor, calls } = fakeCtor(() => {
        throw error;
      });
      vi.stubGlobal('AudioContext', Ctor);

      expect(() => createProductionAudioContext(48000)).toThrow(error);
      expect(calls).toHaveLength(1);
    });

    it('does not mistake a non-DOMException named NotSupportedError for the real thing', () => {
      const lookalike = Object.assign(new Error('nope'), { name: 'NotSupportedError' });
      const { Ctor, calls } = fakeCtor(() => {
        throw lookalike;
      });
      vi.stubGlobal('AudioContext', Ctor);

      expect(() => createProductionAudioContext(48000)).toThrow(lookalike);
      expect(calls).toHaveLength(1);
    });
  });

  it('reports a latency the browser does not expose as null', () => {
    for (const bad of [undefined, null, Number.NaN, Number.POSITIVE_INFINITY, '0.01']) {
      const { Ctor } = fakeCtor(() => {}, { baseLatency: bad, outputLatency: bad });
      vi.stubGlobal('AudioContext', Ctor);
      const { diagnostics } = createProductionAudioContext(48000);
      expect(diagnostics.baseLatency).toBeNull();
      expect(diagnostics.outputLatency).toBeNull();
    }
  });

  it('builds the context from webkitAudioContext on browsers without the standard name', () => {
    const { Ctor, calls } = fakeCtor();
    vi.stubGlobal('AudioContext', undefined);
    vi.stubGlobal('webkitAudioContext', Ctor);

    const { context } = createProductionAudioContext(48000);
    expect(context).toBeInstanceOf(Ctor);
    expect(calls).toHaveLength(1);
  });
});

describe('resolveAudioContextConstructor', () => {
  it('prefers the standard constructor even when webkitAudioContext exists', () => {
    const standard = fakeCtor().Ctor;
    vi.stubGlobal('AudioContext', standard);
    vi.stubGlobal('webkitAudioContext', fakeCtor().Ctor);
    expect(resolveAudioContextConstructor()).toBe(standard);
  });

  it('falls back to webkitAudioContext', () => {
    const webkit = fakeCtor().Ctor;
    vi.stubGlobal('AudioContext', undefined);
    vi.stubGlobal('webkitAudioContext', webkit);
    expect(resolveAudioContextConstructor()).toBe(webkit);
  });

  it('throws when the browser has neither', () => {
    vi.stubGlobal('AudioContext', undefined);
    vi.stubGlobal('webkitAudioContext', undefined);
    expect(() => resolveAudioContextConstructor()).toThrow('AudioContext is not available');
  });

  it('ignores a webkitAudioContext that is not a constructor', () => {
    vi.stubGlobal('AudioContext', undefined);
    vi.stubGlobal('webkitAudioContext', { not: 'a function' });
    expect(() => resolveAudioContextConstructor()).toThrow();
  });
});

describe('contextNeedsUserGesture', () => {
  it.each([
    ['suspended', true],
    ['interrupted', true], // iOS / PWA
    ['running', false],
    ['closed', false],
    ['', false],
  ])('%j → %s', (state, needs) => {
    expect(contextNeedsUserGesture(state)).toBe(needs);
  });
});

describe('attachAudioContextLifecycle', () => {
  function fakeContext(state: string) {
    const listeners = new Map<string, () => void>();
    return {
      state,
      resume: vi.fn(() => Promise.resolve()),
      addEventListener: vi.fn((type: string, fn: () => void) => listeners.set(type, fn)),
      removeEventListener: vi.fn((type: string) => listeners.delete(type)),
      fire: (type: string) => listeners.get(type)?.(),
      listening: (type: string) => listeners.has(type),
    };
  }
  const attach = (context: ReturnType<typeof fakeContext>, onNeed?: () => void) =>
    attachAudioContextLifecycle(context as unknown as AudioContext, onNeed);

  it('resumes a suspended or interrupted context, and leaves a running one alone', () => {
    for (const state of ['suspended', 'interrupted']) {
      const context = fakeContext(state);
      attach(context).resumeIfNeeded();
      expect(context.resume).toHaveBeenCalledTimes(1);
    }
    for (const state of ['running', 'closed']) {
      const context = fakeContext(state);
      attach(context).resumeIfNeeded();
      expect(context.resume).not.toHaveBeenCalled();
    }
  });

  it('tells the caller when the context drops back to needing a gesture', () => {
    const context = fakeContext('running');
    const onNeed = vi.fn();
    attach(context, onNeed);

    context.fire('statechange');
    expect(onNeed).not.toHaveBeenCalled();

    context.state = 'interrupted';
    context.fire('statechange');
    expect(onNeed).toHaveBeenCalledTimes(1);
  });

  it('works without a callback', () => {
    const context = fakeContext('suspended');
    attach(context);
    expect(() => context.fire('statechange')).not.toThrow();
  });

  it('dispose() removes the statechange listener', () => {
    const context = fakeContext('running');
    const lifecycle = attach(context);
    expect(context.listening('statechange')).toBe(true);
    lifecycle.dispose();
    expect(context.listening('statechange')).toBe(false);
  });
});

describe('formatAudioContextDiagnostics', () => {
  const base = {
    sampleRate: 48000,
    latencyHint: 'interactive' as const,
    baseLatency: null,
    outputLatency: null,
    requestedSampleRate: 48000,
  };

  it.each([
    ['no latency known', base, '48000 Hz'],
    ['output latency', { ...base, outputLatency: 0.0183 }, '48000 Hz · 18 ms out'],
    [
      'base latency when there is no output latency',
      { ...base, baseLatency: 0.011 },
      '48000 Hz · 11 ms out',
    ],
    [
      'output latency over base latency',
      { ...base, baseLatency: 0.011, outputLatency: 0.05 },
      '48000 Hz · 50 ms out',
    ],
    [
      'a zero latency, which is still a latency',
      { ...base, outputLatency: 0 },
      '48000 Hz · 0 ms out',
    ],
    [
      'a rate the browser changed',
      { ...base, sampleRate: 44100, requestedSampleRate: 48000 },
      '44100 Hz (requested 48000)',
    ],
    [
      'the fallback context (nothing requested)',
      { ...base, sampleRate: 44100, requestedSampleRate: null, outputLatency: 0.02 },
      '44100 Hz · 20 ms out',
    ],
  ])('%s', (_name, diagnostics, expected) => {
    expect(formatAudioContextDiagnostics(diagnostics)).toBe(expected);
  });
});
