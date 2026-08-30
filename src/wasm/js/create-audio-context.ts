/**
 * Production AudioContext factory for the WASM audio bridge.
 *
 * Requests interactive latency and a preferred sample rate, retrying without
 * sampleRate when the browser rejects the requested rate (common on Android /
 * Bluetooth / HDMI sinks at 48 kHz).
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

/**
 * Create an AudioContext for real-time archive preview.
 *
 * Does not call decodeAudioData, startRendering, or audioWorklet.addModule.
 */
export function createProductionAudioContext(
  preferredSampleRate: number,
  latencyHint: AudioContextLatencyCategory = 'interactive'
): { context: AudioContext; diagnostics: AudioContextDiagnostics } {
  try {
    const context = new AudioContext({
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

  const context = new AudioContext({ latencyHint });
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
