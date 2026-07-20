import { artistAliases, monthlyFolderDisplay } from '../data/artist-aliases';
import type { SongIndexEntry } from './song-archive';

export interface EnrichedSongEntry extends SongIndexEntry {
  /** Human-readable subcollection (month names, etc.) */
  subcollectionDisplay: string | null;
  /** Canonical artist for search and artist pages */
  artistCanonical: string | null;
  /** Slug for /archive/artists/<slug>/ */
  artistSlug: string | null;
  /** ReBirth version inferred from path (e.g. "1.5", "2.0") */
  sourceVersion: string | null;
  /** Song pack requires a specific mod (Featured MODs folder, etc.) */
  hasModDependency: boolean;
}

function slugify(name: string): string {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

/** Turn a path segment like `By_Source` into `By Source`. */
export function displaySegment(segment: string): string {
  return segment.replace(/_/g, ' ').trim();
}

/** Fix monthly archive folder labels (e.g. JANRUARY → January). */
export function displaySubcollection(raw: string | null): string | null {
  if (!raw) return null;
  return monthlyFolderDisplay[raw] ?? monthlyFolderDisplay[raw.toUpperCase()] ?? displaySegment(raw);
}

function matchArtistAlias(label: string | null): { name: string; slug: string } | null {
  if (!label) return null;
  const normalized = label.trim();
  for (const entry of artistAliases) {
    if (entry.aliases.some((alias) => alias.toLowerCase() === normalized.toLowerCase())) {
      return { name: entry.name, slug: entry.slug };
    }
  }
  return { name: normalized, slug: slugify(normalized) };
}

export function inferSourceVersion(path: string): string | null {
  const match = path.match(/By_Source\/Rebirth_(\d+(?:\.\d+)?)/i);
  if (match) return match[1];
  if (path.includes('By_Source/Archives')) return 'archive';
  if (path.startsWith('Monthly_Archive/')) return 'monthly';
  return null;
}

export function inferModDependency(path: string): boolean {
  return path.includes('Featured_Collections/MODs') || path.includes('/MODs/');
}

export function enrichSong(song: SongIndexEntry): EnrichedSongEntry {
  const artistLabel = song.artist ?? (song.collection === 'Artists' ? song.subcollection : null);
  const artistMatch = matchArtistAlias(artistLabel);

  return {
    ...song,
    subcollectionDisplay: displaySubcollection(song.subcollection),
    artistCanonical: artistMatch?.name ?? null,
    artistSlug: artistMatch?.slug ?? null,
    sourceVersion: inferSourceVersion(song.path),
    hasModDependency: inferModDependency(song.path),
  };
}

export function enrichSongs(songs: SongIndexEntry[]): EnrichedSongEntry[] {
  return songs.map(enrichSong);
}

export interface ArtistIndexEntry {
  slug: string;
  name: string;
  aliases: string[];
  songCount: number;
  folders: string[];
}

/** Build artist index from enriched songs (deduplicated by slug). */
export function buildArtistIndex(songs: EnrichedSongEntry[]): ArtistIndexEntry[] {
  const bySlug = new Map<string, ArtistIndexEntry>();

  for (const entry of artistAliases) {
    bySlug.set(entry.slug, {
      slug: entry.slug,
      name: entry.name,
      aliases: [...entry.aliases],
      songCount: 0,
      folders: [],
    });
  }

  for (const song of songs) {
    if (!song.artistSlug || !song.artistCanonical) continue;
    let row = bySlug.get(song.artistSlug);
    if (!row) {
      row = {
        slug: song.artistSlug,
        name: song.artistCanonical,
        aliases: [song.artistCanonical],
        songCount: 0,
        folders: [],
      };
      bySlug.set(song.artistSlug, row);
    }
    row.songCount += 1;
    const folder = song.path.split('/').slice(0, -1).join('/');
    if (folder && !row.folders.includes(folder)) {
      row.folders.push(folder);
    }
  }

  return Array.from(bySlug.values())
    .filter((a) => a.songCount > 0)
    .sort((a, b) => b.songCount - a.songCount || a.name.localeCompare(b.name));
}
