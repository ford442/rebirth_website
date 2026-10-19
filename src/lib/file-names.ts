/**
 * File-name helpers shared by the player and the WASM bridge.
 */

/** Longest slug we put in a download name. */
const MAX_SLUG_LENGTH = 48;

/**
 * Filesystem-safe base name for a download, from a song title.
 *
 * Lower-case ASCII words joined by single dashes, never empty and never
 * starting or ending with a dash (including after truncation).
 */
export function fileSlug(title: string | null | undefined, fallback = 'rebirth-song'): string {
  const trim = (text: string) => text.replace(/^-+|-+$/g, '');
  const slug = trim(
    trim((title || fallback).toLowerCase().replace(/[^a-z0-9]+/g, '-')).slice(0, MAX_SLUG_LENGTH)
  );
  return slug || fallback;
}

export type DroppedFileKind = 'rbs' | 'rbm';

/**
 * What a dropped file is, judged by its extension.
 *
 * Case-insensitive: ReBirth shipped on Windows, where `SONG.RBS` was the
 * normal spelling and a lot of the archive still uses it.
 */
export function classifyDroppedFile(name: string): DroppedFileKind | null {
  const lower = name.toLowerCase();
  if (lower.endsWith('.rbs')) return 'rbs';
  if (lower.endsWith('.rbm')) return 'rbm';
  return null;
}
