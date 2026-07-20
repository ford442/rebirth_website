/**
 * Single source of truth for archive statistics used across pages.
 *
 * Values are computed at build time from the index JSON files, so they stay
 * consistent between the homepage, archive pages, and history page.
 */

import songsFullIndex from './songs-full-index.json';
import modsFullIndex from './mods-full-index.json';
import modsMetadata from './mods-metadata.json';
import artistsIndex from './artists-index.json';
import { songCollections } from './song-collections';

interface SongEntry {
  filename: string;
  path: string;
  url: string;
  title: string;
  collection: string;
  subcollection: string | null;
  artist: string | null;
  bpm: number | null;
  fileSize: string;
  source: string;
}

interface SongFullIndex {
  songs: SongEntry[];
  meta: {
    totalFiles: number;
    manifestTotal: number | null;
    coveragePercent: number | null;
    source: string;
  };
}

interface ModEntry {
  filename: string;
  size: string;
  hasMeta?: boolean;
  title?: string;
  author?: string;
  tags?: string[];
}

interface ModFullIndex {
  mods: ModEntry[];
}

interface ModMetadataFile {
  mods: Array<{
    filename: string;
    title: string;
    author?: string | null;
    year?: number | null;
    description?: string;
    tags?: string[];
  }>;
}

const songs = (songsFullIndex as unknown as SongFullIndex).songs || [];
const mods = (modsFullIndex as unknown as ModFullIndex).mods || [];
const documentedModsList = (modsMetadata as unknown as ModMetadataFile).mods || [];

export const totalSongsIndexed = songs.length;
export const totalSongsEstimated =
  (songsFullIndex as unknown as SongFullIndex).meta?.manifestTotal ?? totalSongsIndexed;
export const songCoveragePercent =
  (songsFullIndex as unknown as SongFullIndex).meta?.coveragePercent ?? null;

export const totalMods = mods.length;
export const documentedMods = documentedModsList.length;
export const modCoveragePercent =
  totalMods > 0 ? Math.round((documentedMods / totalMods) * 100) : 0;

export const artistCount =
  (artistsIndex as { meta?: { artistCount?: number }; artists?: unknown[] }).meta?.artistCount ??
  (artistsIndex as { artists?: unknown[] }).artists?.length ??
  songCollections.artists?.folders?.length ??
  0;
export const mainCollectionCount = Object.keys(songCollections).length;
export const totalSongFolders = Object.values(songCollections).reduce(
  (sum, section) => sum + (section.folders?.length ?? 0),
  0
);
export const monthlyArchiveCount = songCollections.monthly?.folders?.length ?? 0;

export const latestVersion = '2.0.1';

export interface ArchiveStats {
  totalSongsIndexed: number;
  totalSongsEstimated: number;
  songCoveragePercent: number | null;
  totalMods: number;
  documentedMods: number;
  modCoveragePercent: number;
  artistCount: number;
  mainCollectionCount: number;
  totalSongFolders: number;
  monthlyArchiveCount: number;
  latestVersion: string;
}

export function formatStatRange(value: number): string {
  return `${value.toLocaleString()}+`;
}
