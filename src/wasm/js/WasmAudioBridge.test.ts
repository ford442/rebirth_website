import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { WasmAudioBridge } from './WasmAudioBridge';
import { WasmInitError } from './rbs-init-errors';
import { MAX_DEVICE_PARAM_ID } from './player-studio';

// ── A fake Emscripten module ─────────────────────────────────────
//
// `init()` imports the generated glue from `wasmAudioConfig.glueScriptPath`
// (`/wasm/rbsParser.js` under Vitest). That path is mocked, so the real
// bridge code runs end to end against an engine we control.

const wasm = vi.hoisted(() => {
  const state = {
    factoryCalls: 0,
    /** Set to make the worklet callback report failure (a null node handle). */
    workletFails: false,
    /** Set to make the module factory itself reject. */
    factoryFails: false,
    /** Set to make `engine.init()` throw, after the AudioContext already exists. */
    engineInitFails: false,
    engines: [] as Array<Record<string, ReturnType<typeof vi.fn>>>,
  };

  class FakeEngine {
    init = vi.fn(() => {
      if (state.engineInitFails) throw new Error('engine.init exploded');
    });
    delete = vi.fn();
    play = vi.fn();
    pause = vi.fn();
    stop = vi.fn();
    setVolume = vi.fn();
    setDeviceParam = vi.fn();
    saveRbs = vi.fn(() => new Uint8Array([1, 2, 3]));
    lastSaveError = vi.fn(() => '');
    getPlaybackPosition = vi.fn(() => ({ bar: 2, step: 5 }));
    constructor() {
      state.engines.push(this as unknown as Record<string, ReturnType<typeof vi.fn>>);
    }
  }

  const node = { connect: vi.fn(), disconnect: vi.fn() };

  const factory = async () => {
    state.factoryCalls += 1;
    if (state.factoryFails) throw new Error('module factory exploded');
    return {
      RbsAudioEngine: FakeEngine,
      emscriptenRegisterAudioObject: () => 1,
      emscriptenGetAudioObject: () => node,
      initAudioWorklet: (_ctx: number, _engine: unknown, cb: (handle: number | null) => void) =>
        cb(state.workletFails ? null : 2),
      _malloc: () => 16,
      _free: () => {},
      HEAPU8: new Uint8Array(64),
    };
  };

  return { state, node, factory, FakeEngine };
});

vi.mock('/wasm/rbsParser.js', () => ({ default: wasm.factory }));

// ── Browser globals the bridge touches ───────────────────────────

class FakeAudioContext {
  static instances: FakeAudioContext[] = [];
  state = 'running';
  sampleRate = 44100;
  baseLatency = 0.01;
  outputLatency = 0.02;
  destination = {};
  close = vi.fn(() => Promise.resolve());
  resume = vi.fn(() => Promise.resolve());
  addEventListener = vi.fn();
  removeEventListener = vi.fn();
  createGain = vi.fn(() => ({ gain: { value: 0 }, connect: vi.fn(), disconnect: vi.fn() }));
  constructor() {
    FakeAudioContext.instances.push(this);
  }
}

/** A hand-cranked requestAnimationFrame, so frames only happen when a test says so. */
function installFrameLoop() {
  const frames = new Map<number, FrameRequestCallback>();
  let nextId = 1;
  vi.stubGlobal('requestAnimationFrame', (cb: FrameRequestCallback) => {
    const id = nextId++;
    frames.set(id, cb);
    return id;
  });
  vi.stubGlobal('cancelAnimationFrame', (id: number) => void frames.delete(id));
  return {
    pending: () => frames.size,
    /** Run every callback that was waiting for the next frame. */
    runFrame() {
      const due = [...frames.values()];
      frames.clear();
      due.forEach((cb) => cb(0));
    },
  };
}

let loop: ReturnType<typeof installFrameLoop>;

beforeEach(() => {
  wasm.state.factoryCalls = 0;
  wasm.state.workletFails = false;
  wasm.state.factoryFails = false;
  wasm.state.engineInitFails = false;
  wasm.state.engines.length = 0;
  FakeAudioContext.instances.length = 0;

  vi.stubGlobal('AudioContext', FakeAudioContext);
  vi.stubGlobal('AudioWorkletNode', class {});
  vi.stubGlobal(
    'fetch',
    vi.fn(() => Promise.resolve({ ok: true }))
  );
  loop = installFrameLoop();
  vi.spyOn(console, 'error').mockImplementation(() => {});
});

afterEach(() => vi.unstubAllGlobals());

describe('WasmAudioBridge.init', () => {
  it('shares one init between concurrent and repeated calls', async () => {
    const bridge = new WasmAudioBridge();

    const first = bridge.init();
    const second = bridge.init();
    expect(second).toBe(first);
    await Promise.all([first, second]);
    await bridge.init();

    expect(wasm.state.factoryCalls).toBe(1);
    expect(FakeAudioContext.instances).toHaveLength(1);
    expect(wasm.state.engines).toHaveLength(1);
    expect(wasm.state.engines[0].init).toHaveBeenCalledTimes(1);
    expect(bridge.playerStatus).toBe('ready');
  });

  it('does not leak a second AudioContext when init() is called mid-flight', async () => {
    const bridge = new WasmAudioBridge();
    const pending = bridge.init();
    await bridge.init(); // a second caller that never saw the first
    await pending;
    expect(FakeAudioContext.instances).toHaveLength(1);
    expect(FakeAudioContext.instances[0].close).not.toHaveBeenCalled();
  });

  describe('a failure part-way through', () => {
    it('releases the AudioContext and the engine itself, not the caller', async () => {
      wasm.state.workletFails = true;
      const bridge = new WasmAudioBridge();

      const failure = await bridge.init().catch((err: unknown) => err);

      expect(failure).toBeInstanceOf(WasmInitError);
      expect((failure as WasmInitError).failure.reason).toBe('worklet-init-failed');

      // Everything init() had built by then is gone.
      expect(FakeAudioContext.instances).toHaveLength(1);
      expect(FakeAudioContext.instances[0].close).toHaveBeenCalledTimes(1);
      expect(wasm.state.engines[0].delete).toHaveBeenCalledTimes(1);
      expect(bridge.enginePtr).toBeNull();
      expect(bridge.ctx).toBeNull();
      expect(bridge.masterGain).toBeNull();
      expect(bridge.wasmModule).toBeNull();
      expect(bridge.audioDiagnostics).toBeNull();
      expect(bridge.playerStatus).toBe('error');
      expect(loop.pending()).toBe(0);
    });

    it('wraps an unexpected failure as engine-init-failed and still cleans up', async () => {
      wasm.state.factoryFails = true;
      const bridge = new WasmAudioBridge();
      const failure = (await bridge.init().catch((err: unknown) => err)) as WasmInitError;
      expect(failure.failure.reason).toBe('engine-init-failed');
      expect(failure.failure.cause).toBeInstanceOf(Error);
      expect(bridge.playerStatus).toBe('error');
    });

    it('puts the audio device in the message when the context got that far', async () => {
      wasm.state.engineInitFails = true;
      const bridge = new WasmAudioBridge();
      const failure = (await bridge.init().catch((err: unknown) => err)) as WasmInitError;

      expect(failure.failure.reason).toBe('engine-init-failed');
      expect(failure.failure.message).toContain('44100 Hz');
      expect(failure.failure.message).toContain('hint=interactive');
      // ...and the context it created is released regardless.
      expect(FakeAudioContext.instances[0].close).toHaveBeenCalledTimes(1);
    });

    it('lets init() be tried again, and the retry works from a clean slate', async () => {
      wasm.state.workletFails = true;
      const bridge = new WasmAudioBridge();
      await expect(bridge.init()).rejects.toBeInstanceOf(WasmInitError);

      wasm.state.workletFails = false;
      await expect(bridge.init()).resolves.toBeUndefined();

      expect(FakeAudioContext.instances).toHaveLength(2);
      expect(FakeAudioContext.instances[0].close).toHaveBeenCalledTimes(1);
      expect(FakeAudioContext.instances[1].close).not.toHaveBeenCalled();
      expect(bridge.playerStatus).toBe('ready');
      expect(bridge.ctx).toBe(FakeAudioContext.instances[1]);
    });

    it('is tolerated by dispose() afterwards', async () => {
      wasm.state.workletFails = true;
      const bridge = new WasmAudioBridge();
      await bridge.init().catch(() => {});
      expect(() => bridge.dispose()).not.toThrow();
      expect(bridge.playerStatus).toBe('idle');
      // Nothing is closed or deleted twice.
      expect(FakeAudioContext.instances[0].close).toHaveBeenCalledTimes(1);
      expect(wasm.state.engines[0].delete).toHaveBeenCalledTimes(1);
    });
  });

  it('refuses early, without building anything, when AudioWorklet is missing', async () => {
    vi.stubGlobal('AudioWorkletNode', undefined);
    const bridge = new WasmAudioBridge();
    const failure = (await bridge.init().catch((err: unknown) => err)) as WasmInitError;
    expect(failure.failure.reason).toBe('worklet-unavailable');
    expect(FakeAudioContext.instances).toHaveLength(0);
    expect(wasm.state.factoryCalls).toBe(0);
  });

  it('reports missing binaries as wasm-unavailable', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve({ ok: false }))
    );
    const failure = (await new WasmAudioBridge()
      .init()
      .catch((err: unknown) => err)) as WasmInitError;
    expect(failure.failure.reason).toBe('wasm-unavailable');
  });
});

describe('WasmAudioBridge position polling', () => {
  async function readyBridge() {
    const bridge = new WasmAudioBridge();
    await bridge.init();
    return bridge;
  }

  it('does not poll at all until playback starts', async () => {
    await readyBridge();
    expect(loop.pending()).toBe(0);
    loop.runFrame();
    expect(loop.pending()).toBe(0);
  });

  it('polls once per frame while playing and reports the position', async () => {
    const bridge = await readyBridge();
    const onPosition = vi.fn();
    bridge.setPositionCallback(onPosition);

    bridge.play();
    expect(bridge.playerStatus).toBe('playing');
    expect(loop.pending()).toBe(1);

    loop.runFrame();
    loop.runFrame();
    expect(onPosition).toHaveBeenCalledTimes(2);
    expect(onPosition).toHaveBeenLastCalledWith(2, 5);
    expect(loop.pending()).toBe(1);
  });

  it('never schedules a second loop when play() is called twice', async () => {
    const bridge = await readyBridge();
    bridge.play();
    bridge.play();
    expect(loop.pending()).toBe(1);
  });

  it('stops polling on stop() and on pause()', async () => {
    const bridge = await readyBridge();
    const onPosition = vi.fn();
    bridge.setPositionCallback(onPosition);

    bridge.play();
    bridge.stop();
    expect(loop.pending()).toBe(0);
    loop.runFrame();
    expect(onPosition).not.toHaveBeenCalled();

    bridge.play();
    expect(loop.pending()).toBe(1);
    bridge.pause();
    expect(loop.pending()).toBe(0);
  });

  it('starts a fresh loop on the next play()', async () => {
    const bridge = await readyBridge();
    const onPosition = vi.fn();
    bridge.setPositionCallback(onPosition);

    bridge.play();
    bridge.pause();
    bridge.play();
    loop.runFrame();
    expect(onPosition).toHaveBeenCalledTimes(1);
  });

  it('stops polling when the bridge is disposed', async () => {
    const bridge = await readyBridge();
    bridge.play();
    bridge.dispose();
    expect(loop.pending()).toBe(0);
  });

  it('does not start a loop for a bridge with no engine', () => {
    new WasmAudioBridge().play();
    expect(loop.pending()).toBe(0);
  });
});

describe('WasmAudioBridge.dispose', () => {
  it('releases everything and leaves the bridge ready for a fresh init', async () => {
    const bridge = new WasmAudioBridge();
    await bridge.init();
    bridge.dispose();

    expect(FakeAudioContext.instances[0].close).toHaveBeenCalledTimes(1);
    expect(wasm.state.engines[0].delete).toHaveBeenCalledTimes(1);
    expect(bridge.enginePtr).toBeNull();
    expect(bridge.playerStatus).toBe('idle');

    await bridge.init();
    expect(FakeAudioContext.instances).toHaveLength(2);
    expect(bridge.playerStatus).toBe('ready');
  });

  it('survives an AudioContext that is already closed', async () => {
    const bridge = new WasmAudioBridge();
    await bridge.init();
    FakeAudioContext.instances[0].close.mockRejectedValueOnce(
      new DOMException('closed', 'InvalidStateError')
    );
    expect(() => bridge.dispose()).not.toThrow();
    await Promise.resolve(); // the rejection must not escape as unhandled
  });
});

describe('WasmAudioBridge.setDeviceParam', () => {
  it('clamps the device to 0–3 and the param to the DeviceParam table', async () => {
    const bridge = new WasmAudioBridge();
    await bridge.init();

    expect(bridge.setDeviceParam(7, 99, 2)).toBe(true);
    expect(bridge.setDeviceParam(-1, -4, -1)).toBe(true);

    const engine = wasm.state.engines[0].setDeviceParam;
    expect(engine).toHaveBeenNthCalledWith(1, 3, MAX_DEVICE_PARAM_ID, 1);
    expect(engine).toHaveBeenNthCalledWith(2, 0, 0, 0);
    expect(MAX_DEVICE_PARAM_ID).toBe(9);
  });

  it('treats a non-finite value as 0', async () => {
    const bridge = new WasmAudioBridge();
    await bridge.init();
    bridge.setDeviceParam(0, 1, Number.NaN);
    expect(wasm.state.engines[0].setDeviceParam).toHaveBeenCalledWith(0, 1, 0);
  });

  it('is a no-op that says so before init', () => {
    expect(new WasmAudioBridge().setDeviceParam(0, 1, 0.5)).toBe(false);
  });
});

describe('WasmAudioBridge.saveRbs', () => {
  it('names the download after the (slugged) song title', async () => {
    const bridge = new WasmAudioBridge();
    await bridge.init();
    expect(bridge.saveRbs().filename).toBe('rebirth-song-exported.rbs');
  });
});
