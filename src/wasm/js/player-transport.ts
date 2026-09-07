/**
 * Transport controls for the in-browser player: status LED/LCD, toasts,
 * playback position visuals, and the play/stop/load/volume/tempo buttons.
 *
 * Knows nothing about device knobs or the pattern grid — see
 * `player-studio-view.ts` for that.
 */

import { WasmAudioBridge } from './WasmAudioBridge';
import { DegradedRbsPlayer } from './DegradedRbsPlayer';
import { formatAudioContextDiagnostics } from './create-audio-context';
import { INIT_FAILURE_MESSAGES, type InitFailureReason } from './rbs-init-errors';
import type { ToastDetail, ToastVariant } from '../../lib/player-events';
import type { PlayerStatus } from '../types/wasm-audio';
import type { PlayerDom } from './player-dom';

/** Either the real WASM bridge or the degraded Web-Audio fallback player. */
export type PlayerBridge = WasmAudioBridge | DegradedRbsPlayer;

export interface TransportDeps {
  dom: PlayerDom;
  getBridge: () => PlayerBridge;
  isDegradedMode: () => boolean;
  canSketch: () => boolean;
  isSongLoaded: () => boolean;
  markUserGesture: () => void;
  /** Called after the Stop button (or transport.stop()) runs — e.g. to re-apply the pattern preview. */
  onAfterStop?: () => void;
}

export interface TransportView {
  setMessage(message: string): void;
  showToast(message: string, variant?: ToastVariant, duration?: number): void;
  /** Dismisses a toast returned by the DOM (e.g. a still-open "loading" toast), clearing its auto-dismiss timer. */
  dismissToast(toast: HTMLElement): void;
  /** Dismisses every currently-visible "loading" toast (used once a load resolves). */
  dismissLoadingToasts(): void;
  setStatus(status: PlayerStatus): void;
  updatePositionVisuals(bar: number, step: number): void;
  updatePlayAvailability(): void;
  applyAudioDiagnostics(bridge: WasmAudioBridge): void;
  showDegradedFallback(reason: InitFailureReason): void;
  /** Wires the play/stop/load/volume/tempo buttons and the `rb:toast` listener. */
  attachEvents(): void;
}

export function createTransportView(deps: TransportDeps): TransportView {
  const { dom } = deps;
  const toastTimers = new Map<HTMLElement, number>();

  function setMessage(message: string) {
    if (dom.messageEl) dom.messageEl.textContent = message;
  }

  function dismissToast(toast: HTMLElement) {
    const timer = toastTimers.get(toast);
    if (timer) window.clearTimeout(timer);
    toastTimers.delete(toast);
    toast.remove();
  }

  function showToast(message: string, variant: ToastVariant = 'info', duration = 4000) {
    if (!dom.toastStack) {
      setMessage(message);
      return;
    }
    const toast = document.createElement('div');
    toast.className = `player-toast player-toast--${variant}`;
    toast.setAttribute('role', variant === 'error' ? 'alert' : 'status');
    toast.textContent = message;
    dom.toastStack.appendChild(toast);

    if (duration > 0) {
      const timer = window.setTimeout(() => dismissToast(toast), duration);
      toastTimers.set(toast, timer);
    }
  }

  function showDegradedFallback(reason: InitFailureReason) {
    dom.fallbackEl?.classList.remove('is-hidden');
    if (dom.fallbackEl) dom.fallbackEl.dataset.failureReason = reason;
    if (dom.fallbackDetail) dom.fallbackDetail.textContent = INIT_FAILURE_MESSAGES[reason];
    if (dom.fallbackHeadline) {
      dom.fallbackHeadline.textContent =
        reason === 'wasm-unavailable' ? 'WASM engine not built' : 'Full playback unavailable';
    }
    if (dom.fallbackMode) {
      dom.fallbackMode.textContent = deps.canSketch()
        ? 'Degraded mode: metadata + metronome sketch preview (not full ReBirth synthesis).'
        : 'Degraded mode: metadata only — load a file to inspect title and author.';
    }
  }

  function updatePlayAvailability() {
    const { btnPlay } = dom;
    if (!btnPlay) return;
    const degradedMode = deps.isDegradedMode();
    const songLoaded = deps.isSongLoaded();
    const bridge = deps.getBridge();
    if (!degradedMode) {
      btnPlay.disabled =
        !songLoaded && bridge.playerStatus !== 'ready' && bridge.playerStatus !== 'playing';
      return;
    }
    const canSketch = deps.canSketch();
    btnPlay.disabled = !songLoaded || !canSketch;
    btnPlay.title = canSketch
      ? songLoaded
        ? 'Play sketch preview (metronome)'
        : 'Load a song first'
      : 'Sketch preview unavailable — metadata only';
  }

  function setStatus(status: PlayerStatus) {
    const { lcdStatus, ledLabel, btnPlay, btnStop } = dom;
    if (!lcdStatus || !ledLabel) return;

    const degradedMode = deps.isDegradedMode();
    const canSketch = deps.canSketch();
    const songLoaded = deps.isSongLoaded();
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
    const { lcdBar, stepDots, studioGrid, meterSegments } = dom;
    if (lcdBar) lcdBar.textContent = String(bar).padStart(2, '0');
    const currentStep = ((step % 16) + 16) % 16;
    const bridge = deps.getBridge();
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

  function applyAudioDiagnostics(wasmBridge: WasmAudioBridge) {
    const diagnostics = wasmBridge.audioDiagnostics;
    if (!diagnostics || !dom.lcdStatusPanel) return;
    dom.lcdStatusPanel.title = formatAudioContextDiagnostics(diagnostics);
  }

  function attachEvents() {
    const {
      btnPlay,
      btnStop,
      btnLoad,
      fileInput,
      volumeSlider,
      volumeValue,
      tempoSlider,
      tempoValue,
    } = dom;

    window.addEventListener('rb:toast', (event) => {
      const {
        message,
        variant = 'info',
        duration = 4000,
      } = (event as CustomEvent<ToastDetail>).detail;
      showToast(message, variant, duration ?? 4000);
    });

    btnPlay?.addEventListener('click', () => {
      deps.markUserGesture();
      const bridge = deps.getBridge();
      if (deps.isDegradedMode() && !deps.canSketch()) {
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
      deps.markUserGesture();
      deps.getBridge().stop();
      deps.onAfterStop?.();
    });

    btnLoad?.addEventListener('click', () => {
      deps.markUserGesture();
      fileInput?.click();
    });

    volumeSlider?.addEventListener('input', () => {
      const value = Number(volumeSlider.value);
      deps.getBridge().setVolume(value);
      if (volumeValue) volumeValue.textContent = `${Math.round(value * 100)}%`;
    });

    tempoSlider?.addEventListener('input', () => {
      const bpm = Number(tempoSlider.value);
      if (tempoValue) tempoValue.textContent = `${Math.round(bpm)} BPM`;
      const didApply = deps.getBridge().setTempoBpm(bpm);
      if (didApply && dom.lcdBpm) dom.lcdBpm.textContent = String(Math.round(bpm));
    });
  }

  function dismissLoadingToasts() {
    dom.toastStack
      ?.querySelectorAll('.player-toast--loading')
      .forEach((t) => dismissToast(t as HTMLElement));
  }

  return {
    setMessage,
    showToast,
    dismissToast,
    dismissLoadingToasts,
    setStatus,
    updatePositionVisuals,
    updatePlayAvailability,
    applyAudioDiagnostics,
    showDegradedFallback,
    attachEvents,
  };
}
