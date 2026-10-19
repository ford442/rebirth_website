import { describe, expect, it, vi } from 'vitest';
import type { EngineModule, RbsAudioEngineInstance } from '../types/wasm-audio-engine';
import type { ParsedMod } from '../types/wasm-audio-mod';
import {
  loadModIntoEngine,
  loadSongIntoEngine,
  mallocCopy,
  modLoadError,
  modStatusMessage,
} from './wasm-engine-io';

// Indexes into MOD_LOAD_STATUSES (mirrors C++ ModLoadStatus).
const OK = 0;
const NOT_INITIALISED = 1;
const NO_SAMPLES = 2;
const ARENA_EXHAUSTED = 3;
const PARTIAL = 4;

function fakeModule(ptr = 64) {
  const module = {
    _malloc: vi.fn(() => ptr),
    _free: vi.fn(),
    HEAPU8: new Uint8Array(256),
  };
  return module as typeof module & EngineModule;
}

function fakeReport(status: number, loadedSlots: number) {
  return {
    title: 'kit',
    description: '',
    copyright: '',
    resources: { size: () => 0, get: () => undefined, delete: vi.fn() },
    status,
    loadedSlots,
    skinCount: 0,
    usedFrames: 0,
    capacityFrames: 0,
  };
}

function modEngine(status: number, loadedSlots: number) {
  const engine = {
    loadMod: vi.fn(() => status),
    getModReport: vi.fn(() => fakeReport(status, loadedSlots)),
  };
  return engine as typeof engine & RbsAudioEngineInstance;
}

describe('modStatusMessage', () => {
  it.each([
    ['arena-exhausted', /too large for the sample memory budget/],
    ['no-samples', /No playable samples/],
    ['not-initialised', /not ready/],
    ['partial', /could not be loaded/],
    ['something-else', /could not be loaded/],
  ])('%s', (status, pattern) => {
    expect(modStatusMessage(status)).toMatch(pattern);
  });
});

describe('modLoadError', () => {
  const report = (loadedSlots: number) => ({ loadedSlots }) as ParsedMod;

  it('is null for any mod that supplied at least one sample', () => {
    expect(modLoadError(OK, report(3))).toBeNull();
    // A partial load is usable: unfilled slots keep their procedural voices.
    expect(modLoadError(PARTIAL, report(1))).toBeNull();
    expect(modLoadError(ARENA_EXHAUSTED, report(1))).toBeNull();
  });

  it('turns each failing status into its own sentence', () => {
    expect(modLoadError(NO_SAMPLES, report(0))).toEqual({
      code: 'MOD_LOAD_ERROR',
      message: modStatusMessage('no-samples'),
    });
    expect(modLoadError(ARENA_EXHAUSTED, report(0))?.message).toBe(
      modStatusMessage('arena-exhausted')
    );
    expect(modLoadError(NOT_INITIALISED, report(0))?.message).toBe(
      modStatusMessage('not-initialised')
    );
  });

  it('treats a status the table does not know as "no samples"', () => {
    expect(modLoadError(99, report(0))?.message).toBe(modStatusMessage('no-samples'));
  });

  it('still fails an "ok" status that loaded nothing', () => {
    expect(modLoadError(OK, report(0))?.code).toBe('MOD_LOAD_ERROR');
  });
});

describe('loadModIntoEngine', () => {
  const bytes = new Uint8Array([1, 2, 3, 4]);

  it('returns null and frees the heap copy when the mod loaded', () => {
    const module = fakeModule(64);
    const engine = modEngine(OK, 3);

    expect(loadModIntoEngine(module, engine, bytes)).toBeNull();
    expect(engine.loadMod).toHaveBeenCalledWith(64, 4);
    expect(module._free).toHaveBeenCalledWith(64);
  });

  it('reports a mod that supplied no samples, as the live bridge does', () => {
    // It used to ignore loadMod's status, so a bounce silently rendered
    // procedural drums for a kit that had failed to load.
    const module = fakeModule();
    const failure = loadModIntoEngine(module, modEngine(NO_SAMPLES, 0), bytes);
    expect(failure).toEqual({
      code: 'MOD_LOAD_ERROR',
      message: 'No playable samples in this mod (it may be skins only).',
    });
    expect(module._free).toHaveBeenCalledTimes(1);
  });

  it('reports an exhausted sample arena', () => {
    const failure = loadModIntoEngine(fakeModule(), modEngine(ARENA_EXHAUSTED, 0), bytes);
    expect(failure?.message).toBe('Mod is too large for the sample memory budget.');
  });

  it('accepts a partial load', () => {
    expect(loadModIntoEngine(fakeModule(), modEngine(PARTIAL, 2), bytes)).toBeNull();
  });

  it('reports a heap that cannot hold the mod, without calling the engine', () => {
    const module = fakeModule(0);
    const engine = modEngine(OK, 3);
    const failure = loadModIntoEngine(module, engine, new Uint8Array(2048));
    expect(failure).toEqual({
      code: 'MOD_TOO_LARGE',
      message: 'Not enough WASM heap for a 2 KB mod.',
    });
    expect(engine.loadMod).not.toHaveBeenCalled();
    expect(module._free).not.toHaveBeenCalled();
  });

  it('frees the heap copy even when the engine throws', () => {
    const module = fakeModule(64);
    const engine = modEngine(OK, 3);
    engine.loadMod.mockImplementation(() => {
      throw new Error('abort()');
    });
    expect(() => loadModIntoEngine(module, engine, bytes)).toThrow('abort()');
    expect(module._free).toHaveBeenCalledWith(64);
  });
});

describe('loadSongIntoEngine', () => {
  function songEngine(parsed: unknown, lastError = '') {
    const engine = {
      loadSongFromBytes: vi.fn(() => parsed),
      lastParseError: vi.fn(() => lastError),
    };
    return engine as typeof engine & RbsAudioEngineInstance;
  }

  it('reports the parser message when the song is rejected', () => {
    const module = fakeModule(32);
    const failure = loadSongIntoEngine(
      module,
      songEngine(undefined, 'Bad magic'),
      new Uint8Array(4)
    );
    expect(failure).toEqual({ code: 'PARSE_ERROR', message: 'Bad magic' });
    expect(module._free).toHaveBeenCalledWith(32);
  });

  it('falls back to generic copy when the parser gave no reason', () => {
    const failure = loadSongIntoEngine(fakeModule(), songEngine(undefined, ''), new Uint8Array(4));
    expect(failure?.message).toBe('Song parse failed.');
  });

  it('reports a heap that cannot hold the song', () => {
    const module = fakeModule(0);
    const engine = songEngine({});
    expect(loadSongIntoEngine(module, engine, new Uint8Array(900 * 1024))).toEqual({
      code: 'PARSE_ERROR',
      message: 'Not enough WASM heap for a 900 KB song.',
    });
    expect(engine.loadSongFromBytes).not.toHaveBeenCalled();
  });
});

describe('mallocCopy', () => {
  it('copies the bytes onto the heap at the returned pointer', () => {
    const module = fakeModule(8);
    expect(mallocCopy(module, new Uint8Array([9, 8, 7]))).toBe(8);
    expect(Array.from(module.HEAPU8.slice(8, 11))).toEqual([9, 8, 7]);
  });

  it('returns 0 without writing when malloc fails', () => {
    const module = fakeModule(0);
    expect(mallocCopy(module, new Uint8Array([9, 8, 7]))).toBe(0);
    expect(Array.from(module.HEAPU8.slice(0, 3))).toEqual([0, 0, 0]);
  });
});
