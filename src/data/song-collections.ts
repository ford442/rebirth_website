/**
 * Shared song collection data — imported by both server (Astro frontmatter)
 * and client (archive browser script). This eliminates duplication and
 * ensures types flow through to client-side filtering logic.
 */

export interface SongFolder {
  name: string;
  path: string;
  count?: number;
}

export interface SongSection {
  name: string;
  description?: string;
  tags?: string[];
  folders: SongFolder[];
}

export type SongCollectionKey = 'featured' | 'bySource' | 'artists' | 'monthly' | 'other';

export const songCollections: Record<SongCollectionKey, SongSection> = {
  featured: {
    name: 'Featured Collections',
    description: 'Contest winners and curated MOD song packs hand-picked from the community.',
    tags: ['contests', 'curated', 'highlights'],
    folders: [
      { name: 'Contests', path: 'Featured_Collections/Contests', count: 48 },
      { name: 'MODs', path: 'Featured_Collections/MODs', count: 32 },
    ],
  },
  bySource: {
    name: 'By Source',
    description:
      'Songs organized by the ReBirth version they were created with — from 1.5 through 2.0.',
    tags: ['by-version', 'legacy', 'source'],
    folders: [
      { name: 'Rebirth 1.5', path: 'By_Source/Rebirth_1.5', count: 215 },
      { name: 'Rebirth 2.0', path: 'By_Source/Rebirth_2.0', count: 340 },
      { name: 'Archives', path: 'By_Source/Archives', count: 156 },
    ],
  },
  artists: {
    name: 'Artists',
    description: 'Individual artist folders spanning acid, industrial, techno, and melodic styles.',
    tags: ['artists', 'acid', 'industrial', 'techno'],
    folders: [
      { name: 'Cavey', path: 'Artists/Cavey', count: 22 },
      { name: 'DJ Caspa', path: 'Artists/DJ Caspa', count: 14 },
      { name: 'DJ Knightmare', path: 'Artists/DJ Knightmare', count: 18 },
      { name: 'Fanatic', path: 'Artists/Fanatic', count: 26 },
      { name: 'Noah Cohn', path: 'Artists/Noah Cohn', count: 34 },
      { name: 'Noah Cohn Complete', path: 'Artists/Noah_Cohn_Complete', count: 52 },
      { name: 'Noah Cohn Extended', path: 'Artists/Noah_Cohn_Extended', count: 41 },
      { name: 'Rotorkopf', path: 'Artists/Rotorkopf', count: 28 },
    ],
  },
  monthly: {
    name: 'Monthly Archive',
    description: 'A full year of monthly song drops archived in chronological order.',
    tags: ['monthly', 'chronological', 'archive'],
    folders: [{ name: 'January to December', path: 'Monthly_Archive', count: 624 }],
  },
  other: {
    name: 'Other Collections',
    description: 'Themed packs and special collections: Euro, freestyle, Color X, and more.',
    tags: ['themed', 'packs', 'special'],
    folders: [
      { name: 'Color X', path: 'Other_Collections/Color X', count: 30 },
      { name: 'Euro Pack', path: 'Other_Collections/Euro Pack', count: 24 },
      { name: 'Freestyle Pack', path: 'Other_Collections/Freestyle Pack', count: 22 },
      { name: 'Rotorkopf 2', path: 'Other_Collections/Rotorkopf 2', count: 19 },
      { name: 'Saturday Morning', path: 'Other_Collections/Saturday Morning', count: 16 },
    ],
  },
};

import { RBS_ARCHIVE_BASE_URL } from '../lib/archive-urls';

export const archiveBaseUrl = RBS_ARCHIVE_BASE_URL;
