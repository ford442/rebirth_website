/**
 * Shared player event bus for archive preview UX.
 * Used by ModCard, archive folder pages, and the RbsPlayer component.
 */

import { resolveArchivePlaybackUrl } from './archive-urls';
import { normalizeBase } from './url';

export const PLAYER_SCROLL_TARGET = 'player-zone';

export interface LoadDemoDetail {
  src: string;
  label?: string;
  bpm?: number;
  /** Request playback after load — only honoured after a user gesture on the player */
  autoplay?: boolean;
}

export type ToastVariant = 'info' | 'success' | 'error' | 'loading';

export interface ToastDetail {
  message: string;
  variant?: ToastVariant;
  /** Auto-dismiss after ms (0 = manual dismiss only) */
  duration?: number;
}

export interface PlayUrlParams {
  src: string;
  label?: string;
  bpm?: number;
  autoplay?: boolean;
}

/** Dispatch rb:load-demo to the in-page player (homepage / play page). */
export function dispatchLoadDemo(detail: LoadDemoDetail): void {
  window.dispatchEvent(new CustomEvent<LoadDemoDetail>('rb:load-demo', { detail }));
}

/** Scroll to the browser player section, respecting reduced-motion. */
export function scrollToPlayer(): void {
  const target = document.getElementById(PLAYER_SCROLL_TARGET);
  if (!target) return;
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  target.scrollIntoView({ behavior: reducedMotion ? 'auto' : 'smooth', block: 'start' });
  target.focus({ preventScroll: true });
}

/** Show a hardware-styled toast via the player UI. */
export function dispatchToast(detail: ToastDetail): void {
  window.dispatchEvent(new CustomEvent<ToastDetail>('rb:toast', { detail }));
}

/** Build a deep-link URL for the dedicated /play page. */
export function buildPlayUrl(base: string, params: PlayUrlParams): string {
  const url = new URL(`${base.replace(/\/$/, '')}/play`, window.location.origin);
  url.searchParams.set('src', params.src);
  if (params.label) url.searchParams.set('label', params.label);
  if (typeof params.bpm === 'number') url.searchParams.set('bpm', String(params.bpm));
  if (params.autoplay) url.searchParams.set('autoplay', '1');
  return `${url.pathname}${url.search}`;
}

/** Parse deep-link query params from the current page URL. */
export function parsePlayerQuery(): {
  src: string | null;
  label: string | null;
  bpm: number | null;
  autoplay: boolean;
  playPath: string | null;
} {
  const params = new URLSearchParams(window.location.search);
  const playPath = params.get('play');
  const srcParam = params.get('src');
  const resolvedSrc = srcParam ?? (playPath ? resolvePlayPath(playPath) : null);
  const bpmRaw = params.get('bpm');
  return {
    src: resolvedSrc,
    label: params.get('label'),
    bpm: bpmRaw ? Number(bpmRaw) : null,
    autoplay: params.get('autoplay') === '1',
    playPath,
  };
}

/** Resolve a relative archive path (from ?play=) to a playback URL. */
export function resolvePlayPath(relativePath: string): string {
  const meta =
    typeof document !== 'undefined'
      ? document.querySelector<HTMLMetaElement>('meta[name="base-url"]')?.content
      : undefined;
  const base = normalizeBase(meta ?? '/rebirth_website');
  return resolveArchivePlaybackUrl(base, relativePath);
}

/** Load demo in-page when player exists; otherwise navigate to /play. */
export function previewSong(base: string, detail: LoadDemoDetail): void {
  const hasPlayer = Boolean(document.querySelector('.rbs-player'));
  if (hasPlayer) {
    dispatchLoadDemo(detail);
    scrollToPlayer();
    return;
  }
  window.location.href = buildPlayUrl(base, detail);
}
