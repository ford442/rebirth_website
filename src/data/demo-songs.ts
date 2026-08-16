/**
 * Curated same-origin demo songs for the homepage RbsPlayer.
 * Binaries live under public/archive/rbs-songs/demo/.
 * Remote catalog paths are preserved for browse/deep links.
 */

import { normalizeBase } from '../lib/url';

export interface DemoSongEntry {
  id: string;
  label: string;
  filename: string;
  archivePath: string;
  bpm?: number;
  category: 'official' | 'community';
}

export const DEMO_SONGS: DemoSongEntry[] = [
  {
    id: 'a-taste-of-haste',
    label: 'Action Jackson — A Taste of Haste',
    filename: 'a-taste-of-haste.rbs',
    archivePath: 'By_Source/Rebirth_2.0/Complete/A Taste of Haste.rbs',
    category: 'official',
  },
  {
    id: 'orbiting-your-heart',
    label: 'ElekD — Orbiting your Heart',
    filename: 'orbiting-your-heart.rbs',
    archivePath: 'Monthly_Archive/FEBRUARY/Orbiting your Heart.rbs',
    category: 'official',
  },
  {
    id: 'troublemakers',
    label: 'Micromat — Troublemakers',
    filename: 'troublemakers.rbs',
    archivePath: 'Monthly_Archive/SEPTEMBER/Troublemakers.rbs',
    category: 'official',
  },
  {
    id: 'cavey-3-acid',
    label: 'Cavey — 3 acid',
    filename: 'cavey-3-acid.rbs',
    archivePath: 'Artists/Cavey/3_acid.rbs',
    bpm: 128,
    category: 'community',
  },
  {
    id: 'noah-cohn-20',
    label: 'Noah Cohn — 20',
    filename: 'noah-cohn-20.rbs',
    archivePath: 'Artists/Noah_Cohn_Complete/20.rbs',
    bpm: 132,
    category: 'community',
  },
  {
    id: 'dj-knightmare-amnesya',
    label: 'DJ Knightmare — amnesya',
    filename: 'dj-knightmare-amnesya.rbs',
    archivePath: 'Artists/DJ Knightmare/amnesya.rbs',
    bpm: 134,
    category: 'community',
  },
  {
    id: 'metallicon-city',
    label: 'T.G.ViRUS — Metallicon City',
    filename: 'metallicon-city.rbs',
    archivePath: 'By_Source/Rebirth_2.0/Complete/Metallicon City.rbs',
    category: 'community',
  },
  {
    id: 'propellerhead-008',
    label: 'Propellerhead — #008',
    filename: 'propellerhead-008.rbs',
    archivePath: 'By_Source/Rebirth_2.0/Complete/#008.rbs',
    category: 'official',
  },
];

/** Map demo id → entry for featured-card preview wiring. */
export const DEMO_BY_ID = Object.fromEntries(
  DEMO_SONGS.map((entry) => [entry.id, entry])
) as Record<string, DemoSongEntry>;

/** Same-origin URL for a hosted demo file (works with import.meta.env.BASE_URL). */
export function demoSongUrl(base: string, entry: DemoSongEntry): string {
  const root = normalizeBase(base);
  return `${root}/archive/rbs-songs/demo/${encodeURIComponent(entry.filename)}`;
}

/** Shape expected by RbsPlayer `demos` prop. */
export function toPlayerDemos(base: string): { label: string; src: string; bpm?: number }[] {
  return DEMO_SONGS.map((entry) => ({
    label: entry.label,
    src: demoSongUrl(base, entry),
    ...(entry.bpm !== undefined ? { bpm: entry.bpm } : {}),
  }));
}
