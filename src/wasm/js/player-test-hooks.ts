/**
 * Integration hooks for the shipping-engine browser checks.
 *
 * `tests/wasm-engine.spec.ts` and friends drive the real engine classes from
 * the page context, so they need them on `window`. Keeping that here (rather
 * than in `RbsPlayer.astro`) leaves the Astro component's client script a
 * bootstrap one-liner and keeps engine wiring in TypeScript.
 *
 * The hooks are **opt-in**: a visitor to the production site gets none of
 * them. They are installed only when
 *
 *  - the dev server is running (`import.meta.env.DEV`),
 *  - the page URL carries `?test`, or
 *  - `localStorage['rb-test-hooks'] === '1'` — how `playwright.config.ts`
 *    switches them on for every spec without touching the specs, since CI
 *    runs against the production preview build.
 *
 * Pure TypeScript (the patch codec, the SMF writer, the MIDI map, error
 * classification) is tested with Vitest and needs no hook at all.
 */

import { WasmAudioBridge } from './WasmAudioBridge';
import { DegradedRbsPlayer } from './DegradedRbsPlayer';
import {
  attachAudioContextLifecycle,
  createProductionAudioContext,
  resolveAudioContextConstructor,
} from './create-audio-context';
import { songToMidi } from '../../lib/midi-smf';

/** `localStorage` key that turns the hooks on. */
export const TEST_HOOKS_STORAGE_KEY = 'rb-test-hooks';

/** Query parameter that turns the hooks on for a single page load. */
export const TEST_HOOKS_QUERY_PARAM = 'test';

/** Shape added to `window` by {@link installPlayerTestHooks}. */
export interface PlayerTestHooks {
  WasmAudioBridge: typeof WasmAudioBridge;
  DegradedRbsPlayer: typeof DegradedRbsPlayer;
  createProductionAudioContext: typeof createProductionAudioContext;
  attachAudioContextLifecycle: typeof attachAudioContextLifecycle;
  resolveAudioContextConstructor: typeof resolveAudioContextConstructor;
  songToMidi: typeof songToMidi;
}

/** Whether this page load asked for the test hooks. */
export function testHooksEnabled(): boolean {
  if (import.meta.env.DEV) return true;
  if (typeof window === 'undefined') return false;
  try {
    if (new URLSearchParams(window.location.search).has(TEST_HOOKS_QUERY_PARAM)) return true;
  } catch {
    /* fall through to storage */
  }
  try {
    return window.localStorage.getItem(TEST_HOOKS_STORAGE_KEY) === '1';
  } catch {
    // Storage can throw (private windows, blocked site data).
    return false;
  }
}

/**
 * Exposes the engine entry points used by browser tests, when they were
 * asked for. Idempotent. Returns whether the hooks are installed.
 */
export function installPlayerTestHooks(): boolean {
  if (!testHooksEnabled()) return false;
  const target = window as Window & Partial<PlayerTestHooks>;
  target.WasmAudioBridge = WasmAudioBridge;
  target.DegradedRbsPlayer = DegradedRbsPlayer;
  target.createProductionAudioContext = createProductionAudioContext;
  target.attachAudioContextLifecycle = attachAudioContextLifecycle;
  target.resolveAudioContextConstructor = resolveAudioContextConstructor;
  target.songToMidi = songToMidi;
  return true;
}
