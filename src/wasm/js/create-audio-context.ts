/**
 * Production AudioContext factory for the WASM audio bridge and degraded player.
 *
 * Requests interactive latency and a preferred sample rate, retrying without
 * sampleRate when the browser rejects the requested rate (common on Android /
 * Bluetooth / HDMI sinks at 48 kHz).
 *
 * Does not call decodeAudioData, audioWorklet.addModule, or OfflineAudioContext.
 */

import type { AudioContextDiagnostics } from '../types/wasm-audio';

function readLatencySeconds(
  context: AudioContext,
  property: 'baseLatency' | 'outputLatency'
): number | null {
  const value = context[property];
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function buildDiagnostics(
  context: AudioContext,
  latencyHint: AudioContextLatencyCategory,
  requestedSampleRate: number | null
): AudioContextDiagnostics {
  return {
    sampleRate: context.sampleRate,
    latencyHint,
    baseLatency: readLatencySeconds(context, 'baseLatency'),
    outputLatency: readLatencySeconds(context, 'outputLatency'),
    requestedSampleRate,
  };
}

function isUnsupportedSampleRateError(err: unknown): boolean {
  return err instanceof DOMException && err.name === 'NotSupportedError';
}

/** Prefer the standard ctor; only use webkitAudioContext when it is missing. */
export function resolveAudioContextConstructor(): typeof AudioContext {
  if (typeof AudioContext !== 'undefined') {
    return AudioContext;
  }
  const webkit = (globalThis as unknown as { webkitAudioContext?: typeof AudioContext })
    .webkitAudioContext;
  if (typeof webkit === 'function') {
    return webkit;
  }
  throw new Error('AudioContext is not available');
}

export function contextNeedsUserGesture(state: string): boolean {
  return state === 'suspended' || state === 'interrupted';
}

export interface AudioContextLifecycle {
  dispose: () => void;
  resumeIfNeeded: () => void;
}

/**
 * Resume on the next user gesture for both `suspended` and `interrupted`
 * (iOS / PWA). Call `resumeIfNeeded` from play / load / bounce clicks.
 */
export function attachAudioContextLifecycle(
  context: AudioContext,
  onNeedUserGesture?: () => void
): AudioContextLifecycle {
  const resumeIfNeeded = () => {
    if (contextNeedsUserGesture(context.state)) {
      void context.resume();
    }
  };
  const onStateChange = () => {
    if (contextNeedsUserGesture(context.state)) {
      onNeedUserGesture?.();
    }
  };
  context.addEventListener('statechange', onStateChange);
  return {
    dispose: () => context.removeEventListener('statechange', onStateChange),
    resumeIfNeeded,
  };
}

/**
 * Create an AudioContext for real-time archive preview.
 *
 * Does not call decodeAudioData, startRendering, or audioWorklet.addModule.
 */
export function createProductionAudioContext(
  preferredSampleRate: number,
  latencyHint: AudioContextLatencyCategory = 'interactive'
): { context: AudioContext; diagnostics: AudioContextDiagnostics } {
  const Ctor = resolveAudioContextConstructor();
  try {
    const context = new Ctor({
      sampleRate: preferredSampleRate,
      latencyHint,
    });
    return {
      context,
      diagnostics: buildDiagnostics(context, latencyHint, preferredSampleRate),
    };
  } catch (err) {
    if (!isUnsupportedSampleRateError(err)) {
      throw err;
    }
  }

  const context = new Ctor({ latencyHint });
  return {
    context,
    diagnostics: buildDiagnostics(context, latencyHint, null),
  };
}

/** Format diagnostics for LCD tooltips, e.g. "48000 Hz · 18 ms out". */
export function formatAudioContextDiagnostics(diagnostics: AudioContextDiagnostics): string {
  const latencySec = diagnostics.outputLatency ?? diagnostics.baseLatency;
  const latencyMs =
    latencySec != null ? ` · ${Math.round(latencySec * 1000)} ms out` : '';
  const rateNote =
    diagnostics.requestedSampleRate != null &&
    diagnostics.requestedSampleRate !== diagnostics.sampleRate
      ? ` (requested ${diagnostics.requestedSampleRate})`
      : '';
  return `${diagnostics.sampleRate} Hz${rateNote}${latencyMs}`;
}
