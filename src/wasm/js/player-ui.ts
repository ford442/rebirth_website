/**
 * Client-side player UI controller — extracted from RbsPlayer.astro for reuse.
 * Wires DOM elements to WasmAudioBridge / DegradedRbsPlayer.
 *
 * This module is the composer: it owns the state shared across the whole
 * player (which bridge is active, whether a song is loaded, autoplay
 * gating) and file/demo loading. Rendering is delegated to
 * `player-transport.ts` (status, toasts, play/stop/volume/tempo) and
 * `player-studio-view.ts` (pattern grid, device knobs).
 */

import { WasmAudioBridge } from './WasmAudioBridge';
import { DegradedRbsPlayer } from './DegradedRbsPlayer';
import { classifyInitError, type InitFailureReason } from './rbs-init-errors';
import type { LoadDemoDetail } from '../../lib/player-events';
import { parsePlayerQuery, scrollToPlayer } from '../../lib/player-events';
import type { ParsedSong } from '../types/wasm-audio';
import { queryPlayerDom } from './player-dom';
import { createTransportView, type PlayerBridge } from './player-transport';
import { createStudioView } from './player-studio-view';

export type { PlayerBridge } from './player-transport';

export interface DemoSong {
  label: string;
  src: string;
  bpm?: number;
}

export interface PlayerUIOptions {
  demos?: DemoSong[];
  initialSrc?: string;
  autoplay?: boolean;
}

export function initPlayerUI(playerEl: HTMLElement, options: PlayerUIOptions = {}): void {
  const { demos = [], initialSrc = '', autoplay = false } = options;

  const dom = queryPlayerDom(playerEl);

  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (reducedMotion) playerEl.classList.add('player--reduced-motion');

  let bridge: PlayerBridge = new WasmAudioBridge();
  let degradedMode = false;
  let songLoaded = false;
  let canSketch = false;
  let pendingAutoplay = false;
  let userGestureGranted = false;
  let loadedSong: ParsedSong | null = null;

  function setPlayerMode(mode: 'wasm' | 'degraded-sketch' | 'degraded-metadata') {
    playerEl.dataset.playerMode = mode;
  }

  function markUserGesture() {
    userGestureGranted = true;
    if (pendingAutoplay && songLoaded && bridge.playerStatus === 'ready') {
      pendingAutoplay = false;
      bridge.play();
    }
  }

  const studio = createStudioView({
    dom,
    getLoadedSong: () => loadedSong,
    getBridge: () => bridge,
    isDegradedMode: () => degradedMode,
  });

  const transport = createTransportView({
    dom,
    getBridge: () => bridge,
    isDegradedMode: () => degradedMode,
    canSketch: () => canSketch,
    isSongLoaded: () => songLoaded,
    markUserGesture,
    onAfterStop: () => {
      if (loadedSong) studio.applyPatternPreview(loadedSong);
    },
  });

  bridge.setStatusCallback(transport.setStatus);
  bridge.setPositionCallback(transport.updatePositionVisuals);

  async function loadFile(buffer: ArrayBuffer, sourceLabel?: string) {
    try {
      transport.setMessage(degradedMode ? 'Sniffing .rbs header…' : 'Parsing .rbs payload…');
      const song = await bridge.loadRbsFile(buffer);
      songLoaded = true;
      loadedSong = song;
      studio.renderMetadataPanel(song);
      if (dom.lcdBpm) dom.lcdBpm.textContent = String(Math.round(song.bpm));
      if (dom.tempoSlider) dom.tempoSlider.value = String(Math.round(song.bpm));
      if (dom.tempoValue) dom.tempoValue.textContent = `${Math.round(song.bpm)} BPM`;
      if (dom.lcdBar) dom.lcdBar.textContent = '01';
      transport.dismissLoadingToasts();
      const msg = degradedMode
        ? canSketch
          ? `Loaded — press Play for sketch preview (${sourceLabel || 'local file'})`
          : `Metadata loaded (${sourceLabel || 'local file'})`
        : `Loaded ${sourceLabel || 'song file'} — press Play to hear preview`;
      transport.setMessage(msg);
      transport.showToast(msg, 'success');
      transport.updatePlayAvailability();
    } catch (err) {
      console.error('Failed to load .rbs:', err);
      songLoaded = false;
      loadedSong = null;
      transport.updatePlayAvailability();
      const errMsg = `Failed to parse .rbs${sourceLabel ? ` (${sourceLabel})` : ''}`;
      transport.setMessage(errMsg);
      transport.showToast(errMsg, 'error');
      transport.setStatus('error');
    }
  }

  async function loadDemoSong(url: string, label: string, requestAutoplay = false) {
    transport.showToast(`Fetching: ${label}…`, 'loading', 0);
    transport.setMessage(`Fetching demo: ${label}…`);
    try {
      const res = await fetch(url);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const buf = await res.arrayBuffer();
      await loadFile(buf, label);
      if (requestAutoplay) {
        if (userGestureGranted) {
          bridge.play();
        } else {
          pendingAutoplay = true;
        }
      }
    } catch (err) {
      console.error('Failed to load preview demo:', err);
      transport.dismissLoadingToasts();
      const isCors = err instanceof TypeError;
      const errMsg = isCors
        ? 'Preview blocked (CORS) — download the file or use a local copy'
        : 'Preview fetch failed — try local file load';
      transport.setMessage(errMsg);
      transport.showToast(errMsg, 'error');
      transport.setStatus('error');
      throw err;
    }
  }

  window.addEventListener('rb:load-demo', async (event) => {
    const {
      src,
      label = 'archive preview',
      bpm,
      autoplay: requestAutoplay,
    } = (event as CustomEvent<LoadDemoDetail>).detail;
    if (!src) return;

    if (dom.demoSelect) {
      const existing = Array.from(dom.demoSelect.options).find((option) => option.value === src);
      if (!existing) {
        const opt = document.createElement('option');
        opt.value = src;
        opt.textContent = label;
        if (typeof bpm === 'number') opt.dataset.bpm = String(bpm);
        dom.demoSelect.appendChild(opt);
      }
      dom.demoSelect.value = src;
    }

    try {
      await loadDemoSong(src, label, requestAutoplay);
    } catch {
      /* errors handled in loadDemoSong */
    }
  });

  if (dom.demoSelect) {
    demos.forEach((demo) => {
      const opt = document.createElement('option');
      opt.value = demo.src;
      opt.textContent = demo.label;
      if (demo.bpm) opt.dataset.bpm = String(demo.bpm);
      dom.demoSelect!.appendChild(opt);
    });
  }

  dom.fileInput?.addEventListener('change', (e) => {
    markUserGesture();
    const file = (e.target as HTMLInputElement).files?.[0];
    if (!file) return;
    file.arrayBuffer().then((buffer) => loadFile(buffer, file.name));
  });

  if (dom.dropZone) {
    dom.dropZone.addEventListener('dragover', (e) => {
      e.preventDefault();
      dom.dropZone!.classList.add('drag-over');
    });
    dom.dropZone.addEventListener('dragleave', () => {
      dom.dropZone!.classList.remove('drag-over');
    });
    dom.dropZone.addEventListener('drop', (e) => {
      e.preventDefault();
      markUserGesture();
      dom.dropZone!.classList.remove('drag-over');
      const file = e.dataTransfer?.files[0];
      if (file && file.name.endsWith('.rbs')) {
        file.arrayBuffer().then((buffer) => loadFile(buffer, file.name));
      }
    });
  }

  dom.btnDemoLoad?.addEventListener('click', async () => {
    markUserGesture();
    if (!dom.demoSelect?.value) return;
    try {
      await loadDemoSong(
        dom.demoSelect.value,
        dom.demoSelect.selectedOptions[0]?.textContent || 'demo song'
      );
    } catch {
      /* handled */
    }
  });

  transport.attachEvents();
  studio.attachEvents();

  async function activateDegradedMode(reason: InitFailureReason) {
    if ('dispose' in bridge) bridge.dispose();
    bridge = new DegradedRbsPlayer({ failureReason: reason });
    canSketch = bridge.canPlayAudio;
    bridge.setStatusCallback(transport.setStatus);
    bridge.setPositionCallback(transport.updatePositionVisuals);
    degradedMode = true;
    transport.showDegradedFallback(reason);
    setPlayerMode(canSketch ? 'degraded-sketch' : 'degraded-metadata');
    studio.setStudioLive(false);
    transport.setMessage('Initialising degraded preview…');
    await bridge.init();
    bridge.setVolume(Number(dom.volumeSlider?.value ?? bridge.outputVolume));
    if (dom.tempoSlider) dom.tempoSlider.disabled = false;
    if (dom.volumeSlider) dom.volumeSlider.disabled = !canSketch;
    if (dom.tempoValue)
      dom.tempoValue.textContent = `${Math.round(bridge.getTempoBpm() ?? 125)} BPM`;
    if (!demos.length && dom.btnDemoLoad) dom.btnDemoLoad.disabled = false;
    if (!demos.length)
      transport.setMessage('Degraded mode — drop an .rbs file to inspect metadata.');
  }

  async function bootstrap() {
    try {
      transport.setMessage('Initialising audio engine…');
      await bridge.init();
      if (bridge instanceof WasmAudioBridge) {
        transport.applyAudioDiagnostics(bridge);
      }
      setPlayerMode('wasm');
      studio.setStudioLive(true);
      bridge.setVolume(Number(dom.volumeSlider?.value ?? bridge.outputVolume));

      const tempoSupported = bridge.canSetTempo();
      if (dom.tempoSlider) {
        dom.tempoSlider.disabled = !tempoSupported;
        dom.tempoSlider.title = tempoSupported
          ? 'Set playback tempo (BPM)'
          : 'Tempo control will unlock when WASM tempo API is implemented';
      }
      if (tempoSupported) {
        const engineBpm = bridge.getTempoBpm();
        if (typeof engineBpm === 'number' && engineBpm > 0) {
          if (dom.tempoSlider) dom.tempoSlider.value = String(Math.round(engineBpm));
          if (dom.tempoValue) dom.tempoValue.textContent = `${Math.round(engineBpm)} BPM`;
        }
      } else if (dom.tempoValue) {
        dom.tempoValue.textContent = 'WASM TBD';
      }

      if (!demos.length && dom.btnDemoLoad) dom.btnDemoLoad.disabled = true;
      if (!demos.length)
        transport.setMessage('No demos configured — drop an .rbs file to preview.');

      const query = parsePlayerQuery();
      const srcUrl = initialSrc || query.src;
      const shouldAutoplay = autoplay || query.autoplay;

      if (srcUrl) {
        const label = query.label || 'linked preview';
        await loadDemoSong(srcUrl, label, shouldAutoplay);
        if (query.src || query.playPath || initialSrc) scrollToPlayer();
      } else if (demos[0] && dom.demoSelect) {
        dom.demoSelect.value = demos[0].src;
      }
    } catch (err) {
      console.error('WASM init failed:', err);
      const failure = classifyInitError(err);
      await activateDegradedMode(failure.reason);

      const query = parsePlayerQuery();
      const srcUrl = initialSrc || query.src;
      if (srcUrl) {
        try {
          await loadDemoSong(srcUrl, query.label || 'linked preview', autoplay || query.autoplay);
        } catch {
          /* handled */
        }
      }
    }
  }

  void bootstrap();
}
