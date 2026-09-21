/**
 * Shareable studio patches (`?p=` on /play).
 *
 * A link should be able to reconstruct the **working copy** — the steps the
 * visitor rewrote and the knobs they moved — on top of whatever catalog file
 * `?play=` / `?src=` already names. It deliberately does not carry the song
 * itself: a `ParsedSong` round-tripped through Embind would lose TRAK
 * automation, and a whole song does not fit in a query string anyway.
 *
 * Wire format: compact JSON → raw deflate (fflate, MIT, ~8 kB, no WASM) →
 * Base64URL. Tuple arrays rather than objects, because the field names would
 * otherwise be most of the payload.
 *
 *   [deviceIndex, bank, pattern, step, note, drumExtra, flags]   — a step
 *   [deviceIndex, paramId, value]                                — a knob
 *
 * If the compressed patch does not fit {@link MAX_PATCH_BYTES}, callers must
 * fall back to a SAVE `.rbs` download rather than emitting a link browsers
 * and proxies may truncate.
 */

import { deflateSync, inflateSync } from 'fflate';

/** Query parameter carrying the patch. */
export const PATCH_PARAM = 'p';

/**
 * Largest encoded patch we will put in a URL.
 *
 * 2 kB of Base64 keeps the whole link inside the ~2000-character ceiling
 * that old proxies, chat clients and IE-era bookmarklets still enforce, with
 * room for the `?play=` path alongside it.
 */
export const MAX_PATCH_BYTES = 2048;

export const PATCH_VERSION = 1;

/** Flag bits packed into a step tuple's last field. */
export const STEP_FLAG_ACTIVE = 1;
export const STEP_FLAG_ACCENT = 2;
export const STEP_FLAG_SLIDE = 4;

export interface PatchStep {
  deviceIndex: number;
  bank: number;
  patternIndex: number;
  stepIndex: number;
  note: number;
  drumExtra: number;
  active: boolean;
  accent: boolean;
  slide: boolean;
}

export interface PatchParam {
  deviceIndex: number;
  paramId: number;
  value: number;
}

export interface StudioPatch {
  version: number;
  /** Session tempo, when the visitor moved it off the song's own BPM. */
  bpm?: number;
  steps: PatchStep[];
  params: PatchParam[];
}

type StepTuple = [number, number, number, number, number, number, number];
type ParamTuple = [number, number, number];
interface WirePatch {
  v: number;
  b?: number;
  s?: StepTuple[];
  k?: ParamTuple[];
}

export function emptyPatch(): StudioPatch {
  return { version: PATCH_VERSION, steps: [], params: [] };
}

export function isPatchEmpty(patch: StudioPatch): boolean {
  return patch.steps.length === 0 && patch.params.length === 0 && patch.bpm === undefined;
}

function toBase64Url(bytes: Uint8Array): string {
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function fromBase64Url(text: string): Uint8Array | null {
  const padded = text.replace(/-/g, '+').replace(/_/g, '/');
  try {
    const binary = atob(padded + '='.repeat((4 - (padded.length % 4)) % 4));
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
  } catch {
    return null;
  }
}

function clampByte(value: number): number {
  return Math.max(0, Math.min(255, Math.round(value || 0)));
}

export function encodeStudioPatch(patch: StudioPatch): string {
  const wire: WirePatch = { v: PATCH_VERSION };
  if (typeof patch.bpm === 'number' && Number.isFinite(patch.bpm)) {
    wire.b = Math.round(patch.bpm);
  }
  if (patch.steps.length > 0) {
    wire.s = patch.steps.map((step): StepTuple => {
      let flags = 0;
      if (step.active) flags |= STEP_FLAG_ACTIVE;
      if (step.accent) flags |= STEP_FLAG_ACCENT;
      if (step.slide) flags |= STEP_FLAG_SLIDE;
      return [
        clampByte(step.deviceIndex),
        clampByte(step.bank),
        clampByte(step.patternIndex),
        clampByte(step.stepIndex),
        clampByte(step.note),
        clampByte(step.drumExtra),
        flags,
      ];
    });
  }
  if (patch.params.length > 0) {
    // Three decimals is finer than any knob the UI exposes and keeps each
    // value to five JSON characters.
    wire.k = patch.params.map(
      (param): ParamTuple => [
        clampByte(param.deviceIndex),
        clampByte(param.paramId),
        Math.round(param.value * 1000) / 1000,
      ]
    );
  }
  const json = new TextEncoder().encode(JSON.stringify(wire));
  return toBase64Url(deflateSync(json, { level: 9 }));
}

export function decodeStudioPatch(encoded: string): StudioPatch | null {
  if (!encoded) return null;
  const bytes = fromBase64Url(encoded);
  if (!bytes) return null;
  let wire: WirePatch;
  try {
    wire = JSON.parse(new TextDecoder().decode(inflateSync(bytes))) as WirePatch;
  } catch {
    return null;
  }
  if (!wire || typeof wire !== 'object' || wire.v !== PATCH_VERSION) return null;

  const patch = emptyPatch();
  if (typeof wire.b === 'number' && Number.isFinite(wire.b)) patch.bpm = wire.b;
  for (const tuple of wire.s ?? []) {
    if (!Array.isArray(tuple) || tuple.length < 7) continue;
    const [deviceIndex, bank, patternIndex, stepIndex, note, drumExtra, flags] = tuple;
    if (deviceIndex < 0 || deviceIndex > 3) continue;
    if (stepIndex < 0 || stepIndex > 15) continue;
    patch.steps.push({
      deviceIndex,
      bank,
      patternIndex,
      stepIndex,
      note,
      drumExtra,
      active: (flags & STEP_FLAG_ACTIVE) !== 0,
      accent: (flags & STEP_FLAG_ACCENT) !== 0,
      slide: (flags & STEP_FLAG_SLIDE) !== 0,
    });
  }
  for (const tuple of wire.k ?? []) {
    if (!Array.isArray(tuple) || tuple.length < 3) continue;
    const [deviceIndex, paramId, value] = tuple;
    if (deviceIndex < 0 || deviceIndex > 3) continue;
    if (!Number.isFinite(value)) continue;
    patch.params.push({ deviceIndex, paramId, value });
  }
  return patch;
}

export interface ShareResult {
  ok: boolean;
  url?: string;
  /** Encoded length in characters, so the caller can explain a refusal. */
  size: number;
  reason?: 'empty' | 'too-large';
}

/**
 * Layer `?p=` onto the URL that is already open.
 *
 * `?play=` / `?src=` (and the `/rebirth_website` base path) are left exactly
 * as they are: the patch is an overlay on the catalog deep-link, not a
 * replacement for it.
 */
export function buildShareUrl(currentUrl: string, patch: StudioPatch): ShareResult {
  if (isPatchEmpty(patch)) return { ok: false, size: 0, reason: 'empty' };
  const encoded = encodeStudioPatch(patch);
  if (encoded.length > MAX_PATCH_BYTES) {
    return { ok: false, size: encoded.length, reason: 'too-large' };
  }
  const url = new URL(currentUrl);
  url.searchParams.set(PATCH_PARAM, encoded);
  return { ok: true, url: url.toString(), size: encoded.length };
}

/** Read `?p=` out of a query string (`location.search` or a full URL's search). */
export function readPatchFromSearch(search: string): StudioPatch | null {
  const encoded = new URLSearchParams(search).get(PATCH_PARAM);
  return encoded ? decodeStudioPatch(encoded) : null;
}
