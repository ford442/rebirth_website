// @vitest-environment happy-dom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { initPlayerUI } from './player-ui';

// ── Fake bridges ─────────────────────────────────────────────────
//
// `initPlayerUI` is the real thing; only the two audio back-ends are faked.
// That is the part under test: how the composer reacts to what they do.

const h = vi.hoisted(() => {
  const song = {
    title: 'Test Song',
    author: 'Tester',
    bpm: 130,
    devices: [],
    patterns: [],
    arrangement: [],
  };

  const state = {
    wasm: [] as FakeWasmBridge[],
    degraded: [] as FakeDegradedPlayer[],
    /** How the next WASM bridge's init() behaves. */
    init: (() => Promise.resolve()) as () => Promise<void>,
    loadRbs: ((_buffer: ArrayBuffer) => Promise.resolve(song)) as (
      buffer: ArrayBuffer
    ) => Promise<unknown>,
  };

  class FakeBase {
    playerStatus = 'idle';
    outputVolume = 0.8;
    setStatusCallback = vi.fn();
    setPositionCallback = vi.fn();
    setVolume = vi.fn();
    setTempoBpm = vi.fn(() => true);
    getTempoBpm = vi.fn(() => 130);
    canSetTempo = vi.fn(() => true);
    canBounce = vi.fn(() => false);
    canLoadMod = vi.fn(() => false);
    play = vi.fn();
    pause = vi.fn();
    stop = vi.fn();
    clearMod = vi.fn();
    loadRbsFile = vi.fn((buffer: ArrayBuffer) => state.loadRbs(buffer));
  }

  class FakeWasmBridge extends FakeBase {
    audioDiagnostics = null;
    init = vi.fn(() => state.init());
    dispose = vi.fn();
    loadRbmFile = vi.fn(() => Promise.resolve({ resources: [], loadedSlots: 1, status: 'ok' }));
    constructor() {
      super();
      state.wasm.push(this);
    }
  }

  class FakeDegradedPlayer extends FakeBase {
    canPlayAudio = false;
    init = vi.fn(() => Promise.resolve());
    constructor(public options: { failureReason: string }) {
      super();
      state.degraded.push(this);
    }
  }

  return { song, state, FakeWasmBridge, FakeDegradedPlayer };
});

vi.mock('./WasmAudioBridge', () => ({ WasmAudioBridge: h.FakeWasmBridge }));
vi.mock('./DegradedRbsPlayer', () => ({ DegradedRbsPlayer: h.FakeDegradedPlayer }));

// ── DOM and helpers ──────────────────────────────────────────────

const PLAYER_HTML = `
  <div id="rbsDropZone"></div>
  <input id="rbsFileInput" type="file" />
  <button id="rbsBtnPlay"></button>
  <button id="rbsBtnStop"></button>
  <p id="rbsMessage"></p>
  <div id="rbsToastStack"></div>
  <div class="player-lcd-status"><span class="lcd-value"></span></div>
  <div class="player-led"><span class="rb-led__label"></span></div>
`;

/** Listeners `initPlayerUI` puts on `window`, so each test can take them off again. */
let windowListeners: Array<[string, EventListenerOrEventListenerObject]> = [];

function mountPlayer(options: Parameters<typeof initPlayerUI>[1] = {}): HTMLElement {
  const player = document.createElement('div');
  player.className = 'rbs-player';
  player.innerHTML = PLAYER_HTML;
  document.body.appendChild(player);
  initPlayerUI(player, options);
  return player;
}

const $ = (player: HTMLElement, selector: string) => player.querySelector<HTMLElement>(selector)!;
const message = (player: HTMLElement) => $(player, '#rbsMessage').textContent ?? '';

/** Let every pending promise and zero-delay timer run. */
async function settle() {
  for (let i = 0; i < 20; i += 1) await Promise.resolve();
  await new Promise((resolve) => setTimeout(resolve, 0));
  for (let i = 0; i < 20; i += 1) await Promise.resolve();
}

function deferred<T = void>() {
  let resolve!: (value: T) => void;
  let reject!: (reason: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function respond(status: number) {
  return {
    ok: status >= 200 && status < 300,
    status,
    arrayBuffer: () => Promise.resolve(new Uint8Array([1, 2, 3]).buffer),
  };
}

function dropFile(player: HTMLElement, name: string) {
  const file = new File([new Uint8Array([9, 9, 9])], name);
  const event = new Event('drop', { bubbles: true, cancelable: true });
  Object.defineProperty(event, 'dataTransfer', { value: { files: [file] } });
  $(player, '#rbsDropZone').dispatchEvent(event);
}

function setUrl(search: string) {
  (window as unknown as { happyDOM: { setURL(url: string): void } }).happyDOM.setURL(
    `http://localhost/rebirth_website/play${search}`
  );
}

beforeEach(() => {
  h.state.wasm.length = 0;
  h.state.degraded.length = 0;
  h.state.init = () => Promise.resolve();
  h.state.loadRbs = () => Promise.resolve(h.song);
  document.body.innerHTML = '';
  setUrl('');

  windowListeners = [];
  const add = window.addEventListener.bind(window);
  vi.spyOn(window, 'addEventListener').mockImplementation(
    (type: string, listener: EventListenerOrEventListenerObject, opts?: unknown) => {
      windowListeners.push([type, listener]);
      add(type, listener, opts as AddEventListenerOptions);
    }
  );
  vi.spyOn(console, 'error').mockImplementation(() => {});
});

afterEach(() => {
  for (const [type, listener] of windowListeners) window.removeEventListener(type, listener);
  vi.unstubAllGlobals();
});

// ── Tests ────────────────────────────────────────────────────────

describe('a ?src= deep link that cannot be loaded', () => {
  it('reports a 404 on the LCD and leaves the working WASM bridge alone', async () => {
    const fetchMock = vi.fn(() => Promise.resolve(respond(404)));
    vi.stubGlobal('fetch', fetchMock);
    setUrl('?src=%2Farchive%2Fmissing.rbs');

    const player = mountPlayer();
    await settle();

    // The engine initialised fine; a missing file is not an engine failure.
    expect(h.state.wasm).toHaveLength(1);
    expect(h.state.wasm[0].init).toHaveBeenCalledTimes(1);
    expect(h.state.wasm[0].dispose).not.toHaveBeenCalled();
    expect(h.state.degraded).toHaveLength(0);
    expect(player.dataset.playerMode).toBe('wasm');

    // ...and it is fetched once, not again by a degraded-mode retry.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock).toHaveBeenCalledWith('/archive/missing.rbs');

    expect(player.dataset.loadFailure).toBe('not-found');
    expect(message(player)).toContain('404');
  });

  it('reports being offline the same way', async () => {
    const fetchMock = vi.fn(() => Promise.reject(new TypeError('Failed to fetch')));
    vi.stubGlobal('fetch', fetchMock);
    setUrl('?src=%2Farchive%2Fa.rbs');

    const player = mountPlayer();
    await settle();

    expect(h.state.wasm[0].dispose).not.toHaveBeenCalled();
    expect(h.state.degraded).toHaveLength(0);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(player.dataset.playerMode).toBe('wasm');
    expect(player.dataset.loadFailure).toBe('offline');
  });

  it('reports a server error without degrading', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve(respond(503)))
    );
    setUrl('?src=%2Farchive%2Fa.rbs');

    const player = mountPlayer();
    await settle();

    expect(player.dataset.loadFailure).toBe('http-error');
    expect(message(player)).toContain('503');
    expect(h.state.degraded).toHaveLength(0);
  });

  it('still loads a deep link that does exist', async () => {
    const fetchMock = vi.fn(() => Promise.resolve(respond(200)));
    vi.stubGlobal('fetch', fetchMock);
    setUrl('?src=%2Farchive%2Fa.rbs&label=Acid%20Rain');

    const player = mountPlayer();
    await settle();

    expect(h.state.wasm[0].loadRbsFile).toHaveBeenCalledTimes(1);
    expect(player.dataset.loadFailure).toBeUndefined();
    expect(message(player)).toContain('Acid Rain');
  });
});

describe('when the engine itself fails to initialise', () => {
  it('falls back to the degraded player and loads the deep link there, once', async () => {
    h.state.init = () => Promise.reject(new Error('Failed to fetch'));
    const fetchMock = vi.fn(() => Promise.resolve(respond(200)));
    vi.stubGlobal('fetch', fetchMock);
    setUrl('?src=%2Farchive%2Fa.rbs');

    const player = mountPlayer();
    await settle();

    expect(h.state.wasm[0].dispose).toHaveBeenCalledTimes(1);
    expect(h.state.degraded).toHaveLength(1);
    expect(h.state.degraded[0].options.failureReason).toBe('wasm-load-failed');
    expect(player.dataset.playerMode).toBe('degraded-metadata');
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(h.state.degraded[0].loadRbsFile).toHaveBeenCalledTimes(1);
    expect(h.state.wasm[0].loadRbsFile).not.toHaveBeenCalled();
  });
});

describe('a load that fails inside the engine', () => {
  it('shows the engine message, not "[object Object]"', async () => {
    // WasmAudioBridge rejects with plain { code, message } objects.
    h.state.loadRbs = () =>
      Promise.reject({ code: 'PARSE_ERROR', message: 'Not enough WASM heap for a 900 KB song.' });

    const player = mountPlayer();
    await settle();
    dropFile(player, 'big.rbs');
    await settle();

    expect(message(player)).toContain('Not enough WASM heap for a 900 KB song.');
    expect(message(player)).not.toContain('[object Object]');
    expect($(player, '#rbsToastStack').textContent).toContain('Not enough WASM heap');
    expect(message(player)).toContain('big.rbs');
  });

  it('shows a parser message from a real Error too', async () => {
    h.state.loadRbs = () => Promise.reject(new Error('Unsupported .rbs version 9'));

    const player = mountPlayer();
    await settle();
    dropFile(player, 'odd.rbs');
    await settle();

    expect(message(player)).toContain('Unsupported .rbs version 9');
  });

  it('falls back to the generic copy when the engine said nothing', async () => {
    h.state.loadRbs = () => Promise.reject({});

    const player = mountPlayer();
    await settle();
    dropFile(player, 'odd.rbs');
    await settle();

    expect(message(player)).toContain('not a readable .rbs');
  });
});

describe('a song requested before the engine is ready', () => {
  const load = (src: string) =>
    window.dispatchEvent(new CustomEvent('rb:load-demo', { detail: { src, label: 'Early' } }));

  it('waits for init instead of reporting a corrupt file', async () => {
    const init = deferred();
    h.state.init = () => init.promise;
    const fetchMock = vi.fn(() => Promise.resolve(respond(200)));
    vi.stubGlobal('fetch', fetchMock);

    const player = mountPlayer();
    load('/archive/early.rbs');
    await settle();

    // Still initialising: nothing fetched, nothing parsed, nothing blamed.
    expect(fetchMock).not.toHaveBeenCalled();
    expect(h.state.wasm[0].loadRbsFile).not.toHaveBeenCalled();
    expect(message(player)).not.toMatch(/not initialised|not a readable/i);

    init.resolve();
    await settle();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(h.state.wasm[0].loadRbsFile).toHaveBeenCalledTimes(1);
    expect(message(player)).toContain('Early');
    expect(player.dataset.loadFailure).toBeUndefined();
  });

  it('is served by the degraded player when init then fails', async () => {
    const init = deferred();
    h.state.init = () => init.promise;
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve(respond(200)))
    );

    mountPlayer();
    load('/archive/early.rbs');
    await settle();

    init.reject(new Error('Failed to fetch'));
    await settle();

    expect(h.state.degraded).toHaveLength(1);
    expect(h.state.degraded[0].loadRbsFile).toHaveBeenCalledTimes(1);
    expect(h.state.wasm[0].loadRbsFile).not.toHaveBeenCalled();
  });

  it('waits for a file dropped during init as well', async () => {
    const init = deferred();
    h.state.init = () => init.promise;

    const player = mountPlayer();
    dropFile(player, 'early.rbs');
    await settle();
    expect(h.state.wasm[0].loadRbsFile).not.toHaveBeenCalled();

    init.resolve();
    await settle();
    expect(h.state.wasm[0].loadRbsFile).toHaveBeenCalledTimes(1);
  });
});

describe('dropping files', () => {
  it.each([['song.rbs'], ['SONG.RBS'], ['Song.Rbs']])('loads %s as a song', async (name) => {
    const player = mountPlayer();
    await settle();
    dropFile(player, name);
    await settle();

    expect(h.state.wasm[0].loadRbsFile).toHaveBeenCalledTimes(1);
    expect(message(player)).toContain(name);
  });

  it.each([['kit.rbm'], ['KIT.RBM']])('routes %s to the mod loader', async (name) => {
    const player = mountPlayer();
    await settle();
    dropFile(player, name);
    await settle();

    // canLoadMod() is false in this fake, so it is refused — but as a mod,
    // not silently ignored and not parsed as a song.
    expect(h.state.wasm[0].loadRbsFile).not.toHaveBeenCalled();
    expect(message(player)).toMatch(/mods need the full wasm engine/i);
  });

  it('says so about a file that is neither, instead of ignoring it', async () => {
    const player = mountPlayer();
    await settle();
    dropFile(player, 'notes.txt');
    await settle();

    expect(h.state.wasm[0].loadRbsFile).not.toHaveBeenCalled();
    expect(message(player)).toContain('notes.txt');
    expect(message(player)).toMatch(/\.rbs song or a \.rbm mod/);
  });
});
