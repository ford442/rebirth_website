/**
 * Public integration hooks for the shipping-engine browser checks.
 *
 * `tests/wasm-engine.spec.ts` and friends drive the real engine classes from
 * the page context, so they need them on `window`. Keeping that here (rather
 * than in `RbsPlayer.astro`) leaves the Astro component's client script a
 * bootstrap one-liner and keeps engine wiring in TypeScript.
 *
 * These classes hold no credentials or private state — the player uses the
 * same exports internally.
 */

import { WasmAudioBridge } from './WasmAudioBridge';
import { DegradedRbsPlayer } from './DegradedRbsPlayer';
import {
  attachAudioContextLifecycle,
  createProductionAudioContext,
  resolveAudioContextConstructor,
} from './create-audio-context';
import { songToMidi } from '../../lib/midi-smf';
import { mapMidiMessage } from './player-midi';
import { buildShareUrl, decodeStudioPatch, encodeStudioPatch } from '../../lib/studio-patch';
import { classifyLoadError, RbsFetchError, RbsParseError } from './rbs-init-errors';

/** Shape added to `window` by {@link installPlayerTestHooks}. */
export interface PlayerTestHooks {
  WasmAudioBridge: typeof WasmAudioBridge;
  DegradedRbsPlayer: typeof DegradedRbsPlayer;
  createProductionAudioContext: typeof createProductionAudioContext;
  attachAudioContextLifecycle: typeof attachAudioContextLifecycle;
  resolveAudioContextConstructor: typeof resolveAudioContextConstructor;
  songToMidi: typeof songToMidi;
  mapMidiMessage: typeof mapMidiMessage;
  encodeStudioPatch: typeof encodeStudioPatch;
  decodeStudioPatch: typeof decodeStudioPatch;
  buildShareUrl: typeof buildShareUrl;
  classifyLoadError: typeof classifyLoadError;
  RbsFetchError: typeof RbsFetchError;
  RbsParseError: typeof RbsParseError;
}

/** Idempotently exposes the engine entry points used by browser tests. */
export function installPlayerTestHooks(): void {
  const target = window as Window & Partial<PlayerTestHooks>;
  target.WasmAudioBridge = WasmAudioBridge;
  target.DegradedRbsPlayer = DegradedRbsPlayer;
  target.createProductionAudioContext = createProductionAudioContext;
  target.attachAudioContextLifecycle = attachAudioContextLifecycle;
  target.resolveAudioContextConstructor = resolveAudioContextConstructor;
  target.songToMidi = songToMidi;
  target.mapMidiMessage = mapMidiMessage;
  target.encodeStudioPatch = encodeStudioPatch;
  target.decodeStudioPatch = decodeStudioPatch;
  target.buildShareUrl = buildShareUrl;
  target.classifyLoadError = classifyLoadError;
  target.RbsFetchError = RbsFetchError;
  target.RbsParseError = RbsParseError;
}
