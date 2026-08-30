/**
 * Client-side player UI controller — extracted from RbsPlayer.astro for reuse.
 * Wires DOM elements to WasmAudioBridge / DegradedRbsPlayer.
 */

import { WasmAudioBridge } from './WasmAudioBridge';
import { formatAudioContextDiagnostics } from './create-audio-context';
import { DegradedRbsPlayer } from './DegradedRbsPlayer';
import {
  classifyInitError,
  INIT_FAILURE_MESSAGES,
  type InitFailureReason,
} from './rbs-init-errors';
import type { LoadDemoDetail, ToastDetail, ToastVariant } from '../../lib/player-events';
import { parsePlayerQuery, scrollToPlayer } from '../../lib/player-events';
import type { DeviceId, ParsedSong, PlayerStatus } from '../types/wasm-audio';
import {
  buildStepCells,
  defaultPatternCoords,
  DEVICE_INDEX,
  isAcidDevice,
  pickPattern,
} from './player-studio';

export type PlayerBridge = WasmAudioBridge | DegradedRbsPlayer;

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

const DEVICE_LABELS: Record<string, string> = {
  'tb303-a': '303 A',
  'tb303-b': '303 B',
  tr808: '808',
  tr909: '909',
};

export function initPlayerUI(playerEl: HTMLElement, options: PlayerUIOptions = {}): void {
  const { demos = [], initialSrc = '', autoplay = false } = options;

  const dropZone = playerEl.querySelector('#rbsDropZone') as HTMLElement | null;
  const fileInput = playerEl.querySelector('#rbsFileInput') as HTMLInputElement | null;
  const btnPlay = playerEl.querySelector('#rbsBtnPlay') as HTMLButtonElement | null;
  const btnStop = playerEl.querySelector('#rbsBtnStop') as HTMLButtonElement | null;
  const btnLoad = playerEl.querySelector('#rbsBtnLoad') as HTMLButtonElement | null;
  const metaEl = playerEl.querySelector('#rbsMeta') as HTMLElement | null;
  const metaTitle = playerEl.querySelector('#rbsMetaTitle') as HTMLElement | null;
  const metaAuthor = playerEl.querySelector('#rbsMetaAuthor') as HTMLElement | null;
  const metaBpm = playerEl.querySelector('#rbsMetaBpm') as HTMLElement | null;
  const metaDevices = playerEl.querySelector('#rbsMetaDevices') as HTMLElement | null;
  const messageEl = playerEl.querySelector('#rbsMessage') as HTMLElement | null;
  const demoSelect = playerEl.querySelector('#rbsDemoSelect') as HTMLSelectElement | null;
  const btnDemoLoad = playerEl.querySelector('#rbsBtnDemoLoad') as HTMLButtonElement | null;
  const volumeSlider = playerEl.querySelector('#rbsVolume') as HTMLInputElement | null;
  const volumeValue = playerEl.querySelector('#rbsVolumeValue') as HTMLElement | null;
  const tempoSlider = playerEl.querySelector('#rbsTempo') as HTMLInputElement | null;
  const tempoValue = playerEl.querySelector('#rbsTempoValue') as HTMLElement | null;
  const fallbackEl = playerEl.querySelector('#rbsFallback') as HTMLElement | null;
  const fallbackHeadline = playerEl.querySelector('#rbsFallbackHeadline') as HTMLElement | null;
  const fallbackDetail = playerEl.querySelector('#rbsFallbackDetail') as HTMLElement | null;
  const fallbackMode = playerEl.querySelector('#rbsFallbackMode') as HTMLElement | null;
  const toastStack = playerEl.querySelector('#rbsToastStack') as HTMLElement | null;
  const lcdStatus = playerEl.querySelector('.player-lcd-status .lcd-value') as HTMLElement | null;
  const lcdStatusPanel = playerEl.querySelector('.player-lcd-status') as HTMLElement | null;
  const lcdBpm = playerEl.querySelector('.player-lcd-bpm .lcd-value') as HTMLElement | null;
  const lcdBar = playerEl.querySelector('.player-lcd-bar .lcd-value') as HTMLElement | null;
  const ledLabel = playerEl.querySelector('.player-led') as HTMLElement | null;
  const stepDots = playerEl.querySelectorAll('.step-dot');
  const meterSegments = playerEl.querySelectorAll('.meter-segment');
  const studioGrid = playerEl.querySelector('#rbsStudioGrid') as HTMLElement | null;
  const studioHint = playerEl.querySelector('#rbsStudioHint') as HTMLElement | null;
  const studioBank = playerEl.querySelector('#rbsStudioBank') as HTMLSelectElement | null;
  const studioPattern = playerEl.querySelector('#rbsStudioPattern') as HTMLSelectElement | null;
  const studioTabs = playerEl.querySelectorAll<HTMLButtonElement>('[data-studio-device]');
  const studioKnobs = playerEl.querySelectorAll<HTMLInputElement | HTMLSelectElement>(
    '[data-studio-param]'
  );

  let selectedDevice: DeviceId = 'tb303-a';
  let selectedBank = 0;
  let selectedPatternIndex = 0;

  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (reducedMotion) playerEl.classList.add('player--reduced-motion');

  let bridge: PlayerBridge = new WasmAudioBridge();
  let degradedMode = false;
  let songLoaded = false;
  let canSketch = false;
  let pendingAutoplay = false;
  let userGestureGranted = false;
  let loadedSong: ParsedSong | null = null;
  const toastTimers = new Map<HTMLElement, number>();

  function setMessage(message: string) {
    if (messageEl) messageEl.textContent = message;
  }

  function setPlayerMode(mode: 'wasm' | 'degraded-sketch' | 'degraded-metadata') {
    playerEl.dataset.playerMode = mode;
  }

  function showToast(message: string, variant: ToastVariant = 'info', duration = 4000) {
    if (!toastStack) {
      setMessage(message);
      return;
    }
    const toast = document.createElement('div');
    toast.className = `player-toast player-toast--${variant}`;
    toast.setAttribute('role', variant === 'error' ? 'alert' : 'status');
    toast.textContent = message;
    toastStack.appendChild(toast);

    if (duration > 0) {
      const timer = window.setTimeout(() => dismissToast(toast), duration);
      toastTimers.set(toast, timer);
    }
  }

  function dismissToast(toast: HTMLElement) {
    const timer = toastTimers.get(toast);
    if (timer) window.clearTimeout(timer);
    toastTimers.delete(toast);
    toast.remove();
  }

  window.addEventListener('rb:toast', (event) => {
    const {
      message,
      variant = 'info',
      duration = 4000,
    } = (event as CustomEvent<ToastDetail>).detail;
    showToast(message, variant, duration ?? 4000);
  });

  function showDegradedFallback(reason: InitFailureReason) {
    degradedMode = true;
    fallbackEl?.classList.remove('is-hidden');
    if (fallbackEl) fallbackEl.dataset.failureReason = reason;
    if (fallbackDetail) fallbackDetail.textContent = INIT_FAILURE_MESSAGES[reason];
    if (fallbackHeadline) {
      fallbackHeadline.textContent =
        reason === 'wasm-unavailable' ? 'WASM engine not built' : 'Full playback unavailable';
    }
    if (fallbackMode) {
      fallbackMode.textContent = canSketch
        ? 'Degraded mode: metadata + metronome sketch preview (not full ReBirth synthesis).'
        : 'Degraded mode: metadata only — load a file to inspect title and author.';
    }
    setPlayerMode(canSketch ? 'degraded-sketch' : 'degraded-metadata');
    setStudioLive(false);
  }

  function updatePlayAvailability() {
    if (!btnPlay) return;
    if (!degradedMode) {
      btnPlay.disabled =
        !songLoaded && bridge.playerStatus !== 'ready' && bridge.playerStatus !== 'playing';
      return;
    }
    btnPlay.disabled = !songLoaded || !canSketch;
    btnPlay.title = canSketch
      ? songLoaded
        ? 'Play sketch preview (metronome)'
        : 'Load a song first'
      : 'Sketch preview unavailable — metadata only';
  }

  function markUserGesture() {
    userGestureGranted = true;
    if (pendingAutoplay && songLoaded && bridge.playerStatus === 'ready') {
      pendingAutoplay = false;
      bridge.play();
    }
  }

  function setStatus(status: PlayerStatus) {
    if (!lcdStatus || !ledLabel) return;

    const labelText = ledLabel.querySelector('.rb-led__label');

    switch (status) {
      case 'idle':
        lcdStatus.textContent = 'IDLE';
        ledLabel.className = 'rb-led rb-led--pending player-led';
        if (labelText) labelText.textContent = degradedMode ? 'DEGRADED' : 'PENDING';
        if (btnPlay) {
          btnPlay.textContent = '▶';
          btnPlay.title = 'Play';
          btnPlay.setAttribute('aria-label', 'Play');
        }
        break;
      case 'loading':
        lcdStatus.textContent = 'LOAD…';
        ledLabel.className = 'rb-led rb-led--pending player-led';
        if (labelText) labelText.textContent = 'LOADING';
        setMessage(degradedMode ? 'Reading .rbs metadata…' : 'Loading song data…');
        showToast(degradedMode ? 'Reading .rbs metadata…' : 'Loading song data…', 'loading', 0);
        break;
      case 'ready':
        lcdStatus.textContent = degradedMode ? 'META' : 'READY';
        ledLabel.className = 'rb-led rb-led--online player-led';
        if (labelText)
          labelText.textContent = degradedMode ? (canSketch ? 'SKETCH' : 'META') : 'ONLINE';
        if (btnStop) btnStop.disabled = !songLoaded;
        if (btnPlay) {
          btnPlay.textContent = '▶';
          btnPlay.title = degradedMode && canSketch ? 'Play sketch preview' : 'Play';
          btnPlay.setAttribute('aria-label', 'Play');
        }
        updatePlayAvailability();
        break;
      case 'playing':
        lcdStatus.textContent = degradedMode ? 'SKETCH' : 'PLAY';
        ledLabel.className = 'rb-led rb-led--online player-led';
        if (labelText) labelText.textContent = degradedMode ? 'SKETCH' : 'PLAYING';
        if (btnPlay) {
          btnPlay.textContent = '❚❚';
          btnPlay.title = 'Pause';
          btnPlay.setAttribute('aria-label', 'Pause');
          btnPlay.disabled = false;
        }
        if (btnStop) btnStop.disabled = false;
        setMessage(degradedMode ? 'Sketch preview active (metronome only)' : 'Playback active');
        break;
      case 'error':
        lcdStatus.textContent = 'ERR';
        ledLabel.className = 'rb-led rb-led--offline player-led';
        if (labelText) labelText.textContent = 'ERROR';
        if (degradedMode && !canSketch) {
          setMessage('Playback unavailable in metadata-only mode — inspect file info above.');
        } else if (degradedMode) {
          setMessage('Sketch preview failed — reload the file and try again.');
        } else {
          setMessage('Playback unavailable — check console for details');
        }
        updatePlayAvailability();
        break;
    }
  }

  function updatePositionVisuals(bar: number, step: number) {
    if (lcdBar) lcdBar.textContent = String(bar).padStart(2, '0');
    const currentStep = ((step % 16) + 16) % 16;
    stepDots.forEach((dot, i) => {
      dot.classList.toggle('is-current', i === currentStep);
      if (bridge.playerStatus === 'playing' || bridge.playerStatus === 'ready') {
        dot.classList.toggle('is-active', i <= currentStep);
      }
    });
    studioGrid?.querySelectorAll<HTMLElement>('[data-studio-step]').forEach((cell) => {
      const index = Number(cell.dataset.studioStep);
      cell.classList.toggle('is-current', index === currentStep);
    });
    const litSegments = Math.min(12, Math.max(1, Math.floor(((currentStep + 1) / 16) * 12)));
    meterSegments.forEach((segment, i) => {
      segment.classList.toggle('is-active', i < litSegments);
    });
  }

  function applyPatternPreview(song: ParsedSong) {
    const patternSteps = new Set<number>();
    for (const pattern of song.patterns) {
      pattern.steps.forEach((stepData, index) => {
        if (stepData.active) patternSteps.add(index);
      });
    }
    stepDots.forEach((dot, i) => {
      dot.classList.toggle('is-pattern', patternSteps.has(i));
      if (bridge.playerStatus !== 'playing') {
        dot.classList.toggle('is-active', patternSteps.has(i));
      }
    });
  }

  function renderMetadataPanel(song: ParsedSong) {
    if (metaTitle) metaTitle.textContent = song.title || 'Untitled';
    if (metaAuthor) metaAuthor.textContent = song.author || 'Unknown';
    if (metaBpm) metaBpm.textContent = `${Math.round(song.bpm)} BPM`;
    metaEl?.classList.remove('is-hidden');

    if (metaDevices) {
      metaDevices.innerHTML = '';
      const activeDevices =
        song.devices.length > 0
          ? song.devices
          : [
              {
                deviceId: 'tb303-a' as const,
                knobs: {},
                muted: false,
                level: 0.8,
                pan: 0.5,
                waveform: 0,
                initialPatternBank: 0,
                initialPatternIndex: 0,
              },
            ];

      for (const device of activeDevices) {
        const chip = document.createElement('span');
        chip.className = `meta-device${device.muted ? ' meta-device--muted' : ''}`;
        const label = DEVICE_LABELS[device.deviceId] ?? device.deviceId;
        const patternCount = song.patterns.filter((p) => p.deviceId === device.deviceId).length;
        chip.innerHTML = `<span class="meta-device__led" aria-hidden="true"></span><span class="meta-device__label">${label}</span><span class="meta-device__count">${patternCount}P</span>`;
        chip.title = `${label}${device.muted ? ' (muted)' : ''} — ${patternCount} patterns`;
        metaDevices.appendChild(chip);
      }
    }

    applyPatternPreview(song);
    const coords = defaultPatternCoords(song, selectedDevice);
    selectedBank = coords.bank;
    selectedPatternIndex = coords.patternIndex;
    if (studioBank) studioBank.value = String(selectedBank);
    if (studioPattern) studioPattern.value = String(selectedPatternIndex);
    renderStudio();
  }

  function setStudioLive(live: boolean) {
    playerEl.dataset.studioLive = live ? '1' : '0';
    studioKnobs.forEach((el) => {
      el.disabled = !live;
    });
    if (studioHint) {
      studioHint.textContent = live
        ? 'Session knobs — cutoff, reso, decay, and mixer affect playback. Not saved to the file.'
        : 'Live knobs need the WASM engine. Pattern grid still shows parsed steps when available.';
    }
  }

  function renderStudio() {
    if (!studioGrid) return;
    const pattern = loadedSong
      ? pickPattern(loadedSong, selectedDevice, selectedBank, selectedPatternIndex)
      : null;
    const cells = buildStepCells(pattern, selectedDevice);
    studioGrid.querySelectorAll<HTMLElement>('[data-studio-step]').forEach((cell) => {
      const index = Number(cell.dataset.studioStep);
      const model = cells[index];
      if (!model) return;
      cell.classList.toggle('is-active', model.active);
      const label = cell.querySelector('[data-studio-label]');
      const flags = cell.querySelector('[data-studio-flags]');
      if (label) label.textContent = model.label;
      if (flags) {
        const marks: string[] = [];
        if (model.accent) marks.push('A');
        if (model.slide) marks.push('S');
        flags.textContent = marks.join(' ');
      }
    });

    studioTabs.forEach((tab) => {
      const on = tab.dataset.studioDevice === selectedDevice;
      tab.classList.toggle('is-selected', on);
      tab.setAttribute('aria-selected', on ? 'true' : 'false');
    });

    const device = loadedSong?.devices.find((d) => d.deviceId === selectedDevice);
    const acid = isAcidDevice(selectedDevice);
    playerEl.querySelectorAll<HTMLElement>('[data-knob-wrap]').forEach((wrap) => {
      const key = wrap.dataset.knobWrap;
      const acidOnly =
        key === 'cutoff' || key === 'resonance' || key === 'envMod' || key === 'waveform';
      wrap.hidden = Boolean(acidOnly && !acid);
    });

    const applyKnob = (name: string, value: number | boolean) => {
      const el = playerEl.querySelector<HTMLInputElement | HTMLSelectElement>(
        `[data-studio-knob="${name}"]`
      );
      if (!el) return;
      if (el instanceof HTMLInputElement && el.type === 'checkbox') {
        el.checked = Boolean(value);
      } else {
        el.value = String(value);
      }
    };
    if (device) {
      applyKnob('tune', device.knobs.tune ?? 0.5);
      applyKnob('cutoff', device.knobs.cutoff ?? 0.5);
      applyKnob('resonance', device.knobs.resonance ?? 0.5);
      applyKnob('envMod', device.knobs.envMod ?? 0.5);
      applyKnob('decay', device.knobs.decay ?? 0.5);
      applyKnob('accent', device.knobs.accent ?? 0.5);
      applyKnob('level', device.level ?? 0.8);
      applyKnob('pan', device.pan ?? 0.5);
      applyKnob('waveform', device.waveform ?? 0);
      applyKnob('mute', device.muted);
    }
  }

  bridge.setStatusCallback(setStatus);
  bridge.setPositionCallback(updatePositionVisuals);

  async function loadFile(buffer: ArrayBuffer, sourceLabel?: string) {
    try {
      setMessage(degradedMode ? 'Sniffing .rbs header…' : 'Parsing .rbs payload…');
      const song = await bridge.loadRbsFile(buffer);
      songLoaded = true;
      loadedSong = song;
      renderMetadataPanel(song);
      if (lcdBpm) lcdBpm.textContent = String(Math.round(song.bpm));
      if (tempoSlider) tempoSlider.value = String(Math.round(song.bpm));
      if (tempoValue) tempoValue.textContent = `${Math.round(song.bpm)} BPM`;
      if (lcdBar) lcdBar.textContent = '01';
      toastStack
        ?.querySelectorAll('.player-toast--loading')
        .forEach((t) => dismissToast(t as HTMLElement));
      const msg = degradedMode
        ? canSketch
          ? `Loaded — press Play for sketch preview (${sourceLabel || 'local file'})`
          : `Metadata loaded (${sourceLabel || 'local file'})`
        : `Loaded ${sourceLabel || 'song file'} — press Play to hear preview`;
      setMessage(msg);
      showToast(msg, 'success');
      updatePlayAvailability();
    } catch (err) {
      console.error('Failed to load .rbs:', err);
      songLoaded = false;
      loadedSong = null;
      updatePlayAvailability();
      const errMsg = `Failed to parse .rbs${sourceLabel ? ` (${sourceLabel})` : ''}`;
      setMessage(errMsg);
      showToast(errMsg, 'error');
      setStatus('error');
    }
  }

  async function loadDemoSong(url: string, label: string, requestAutoplay = false) {
    showToast(`Fetching: ${label}…`, 'loading', 0);
    setMessage(`Fetching demo: ${label}…`);
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
      toastStack
        ?.querySelectorAll('.player-toast--loading')
        .forEach((t) => dismissToast(t as HTMLElement));
      const isCors = err instanceof TypeError;
      const errMsg = isCors
        ? 'Preview blocked (CORS) — download the file or use a local copy'
        : 'Preview fetch failed — try local file load';
      setMessage(errMsg);
      showToast(errMsg, 'error');
      setStatus('error');
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

    if (demoSelect) {
      const existing = Array.from(demoSelect.options).find((option) => option.value === src);
      if (!existing) {
        const opt = document.createElement('option');
        opt.value = src;
        opt.textContent = label;
        if (typeof bpm === 'number') opt.dataset.bpm = String(bpm);
        demoSelect.appendChild(opt);
      }
      demoSelect.value = src;
    }

    try {
      await loadDemoSong(src, label, requestAutoplay);
    } catch {
      /* errors handled in loadDemoSong */
    }
  });

  if (demoSelect) {
    demos.forEach((demo) => {
      const opt = document.createElement('option');
      opt.value = demo.src;
      opt.textContent = demo.label;
      if (demo.bpm) opt.dataset.bpm = String(demo.bpm);
      demoSelect.appendChild(opt);
    });
  }

  fileInput?.addEventListener('change', (e) => {
    markUserGesture();
    const file = (e.target as HTMLInputElement).files?.[0];
    if (!file) return;
    file.arrayBuffer().then((buffer) => loadFile(buffer, file.name));
  });

  if (dropZone) {
    dropZone.addEventListener('dragover', (e) => {
      e.preventDefault();
      dropZone.classList.add('drag-over');
    });
    dropZone.addEventListener('dragleave', () => {
      dropZone.classList.remove('drag-over');
    });
    dropZone.addEventListener('drop', (e) => {
      e.preventDefault();
      markUserGesture();
      dropZone.classList.remove('drag-over');
      const file = e.dataTransfer?.files[0];
      if (file && file.name.endsWith('.rbs')) {
        file.arrayBuffer().then((buffer) => loadFile(buffer, file.name));
      }
    });
  }

  btnPlay?.addEventListener('click', () => {
    markUserGesture();
    if (degradedMode && !canSketch) {
      setStatus('error');
      return;
    }
    if (bridge.playerStatus === 'playing') {
      bridge.pause();
    } else {
      bridge.play();
    }
  });

  btnStop?.addEventListener('click', () => {
    markUserGesture();
    bridge.stop();
    if (loadedSong) applyPatternPreview(loadedSong);
  });

  btnLoad?.addEventListener('click', () => {
    markUserGesture();
    fileInput?.click();
  });

  btnDemoLoad?.addEventListener('click', async () => {
    markUserGesture();
    if (!demoSelect?.value) return;
    try {
      await loadDemoSong(
        demoSelect.value,
        demoSelect.selectedOptions[0]?.textContent || 'demo song'
      );
    } catch {
      /* handled */
    }
  });

  volumeSlider?.addEventListener('input', () => {
    const value = Number(volumeSlider.value);
    bridge.setVolume(value);
    if (volumeValue) volumeValue.textContent = `${Math.round(value * 100)}%`;
  });

  studioTabs.forEach((tab) => {
    tab.addEventListener('click', () => {
      const id = tab.dataset.studioDevice as DeviceId | undefined;
      if (!id) return;
      selectedDevice = id;
      if (loadedSong) {
        const coords = defaultPatternCoords(loadedSong, selectedDevice);
        selectedBank = coords.bank;
        selectedPatternIndex = coords.patternIndex;
        if (studioBank) studioBank.value = String(selectedBank);
        if (studioPattern) studioPattern.value = String(selectedPatternIndex);
      }
      renderStudio();
    });
  });

  studioBank?.addEventListener('change', () => {
    selectedBank = Number(studioBank.value) || 0;
    renderStudio();
  });
  studioPattern?.addEventListener('change', () => {
    selectedPatternIndex = Number(studioPattern.value) || 0;
    renderStudio();
  });

  studioKnobs.forEach((el) => {
    const apply = () => {
      if (degradedMode) return;
      const paramId = Number(el.dataset.studioParam);
      if (!Number.isFinite(paramId)) return;
      let value = 0;
      if (el instanceof HTMLInputElement && el.type === 'checkbox') {
        value = el.checked ? 1 : 0;
      } else if (el instanceof HTMLSelectElement) {
        value = Number(el.value);
      } else if (el instanceof HTMLInputElement) {
        value = Number(el.value);
      }
      const deviceIndex = DEVICE_INDEX[selectedDevice];
      const ok =
        'setDeviceParam' in bridge &&
        typeof bridge.setDeviceParam === 'function' &&
        bridge.setDeviceParam(deviceIndex, paramId, value);
      if (ok && loadedSong) {
        const device = loadedSong.devices.find((d) => d.deviceId === selectedDevice);
        const knob = el.dataset.studioKnob;
        if (device && knob) {
          if (knob === 'mute') device.muted = value >= 0.5;
          else if (knob === 'level') device.level = value;
          else if (knob === 'pan') device.pan = value;
          else if (knob === 'waveform') device.waveform = value;
          else device.knobs[knob] = value;
        }
      }
    };
    el.addEventListener('input', apply);
    el.addEventListener('change', apply);
  });

  tempoSlider?.addEventListener('input', () => {
    const bpm = Number(tempoSlider.value);
    if (tempoValue) tempoValue.textContent = `${Math.round(bpm)} BPM`;
    const didApply = bridge.setTempoBpm(bpm);
    if (didApply && lcdBpm) lcdBpm.textContent = String(Math.round(bpm));
  });

  async function activateDegradedMode(reason: InitFailureReason) {
    if ('dispose' in bridge) bridge.dispose();
    bridge = new DegradedRbsPlayer({ failureReason: reason });
    canSketch = bridge.canPlayAudio;
    bridge.setStatusCallback(setStatus);
    bridge.setPositionCallback(updatePositionVisuals);
    showDegradedFallback(reason);
    setMessage('Initialising degraded preview…');
    await bridge.init();
    bridge.setVolume(Number(volumeSlider?.value ?? bridge.outputVolume));
    if (tempoSlider) tempoSlider.disabled = false;
    if (volumeSlider) volumeSlider.disabled = !canSketch;
    if (tempoValue) tempoValue.textContent = `${Math.round(bridge.getTempoBpm() ?? 125)} BPM`;
    if (!demos.length && btnDemoLoad) btnDemoLoad.disabled = false;
    if (!demos.length) setMessage('Degraded mode — drop an .rbs file to inspect metadata.');
  }

  function applyAudioDiagnostics(wasmBridge: WasmAudioBridge) {
    const diagnostics = wasmBridge.audioDiagnostics;
    if (!diagnostics || !lcdStatusPanel) return;
    lcdStatusPanel.title = formatAudioContextDiagnostics(diagnostics);
  }

  async function bootstrap() {
    try {
      setMessage('Initialising audio engine…');
      await bridge.init();
      if (bridge instanceof WasmAudioBridge) {
        applyAudioDiagnostics(bridge);
      }
      setPlayerMode('wasm');
      setStudioLive(true);
      bridge.setVolume(Number(volumeSlider?.value ?? bridge.outputVolume));

      const tempoSupported = bridge.canSetTempo();
      if (tempoSlider) {
        tempoSlider.disabled = !tempoSupported;
        tempoSlider.title = tempoSupported
          ? 'Set playback tempo (BPM)'
          : 'Tempo control will unlock when WASM tempo API is implemented';
      }
      if (tempoSupported) {
        const engineBpm = bridge.getTempoBpm();
        if (typeof engineBpm === 'number' && engineBpm > 0) {
          if (tempoSlider) tempoSlider.value = String(Math.round(engineBpm));
          if (tempoValue) tempoValue.textContent = `${Math.round(engineBpm)} BPM`;
        }
      } else if (tempoValue) {
        tempoValue.textContent = 'WASM TBD';
      }

      if (!demos.length && btnDemoLoad) btnDemoLoad.disabled = true;
      if (!demos.length) setMessage('No demos configured — drop an .rbs file to preview.');

      const query = parsePlayerQuery();
      const srcUrl = initialSrc || query.src;
      const shouldAutoplay = autoplay || query.autoplay;

      if (srcUrl) {
        const label = query.label || 'linked preview';
        await loadDemoSong(srcUrl, label, shouldAutoplay);
        if (query.src || query.playPath || initialSrc) scrollToPlayer();
      } else if (demos[0] && demoSelect) {
        demoSelect.value = demos[0].src;
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
