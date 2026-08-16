/**
 * Canonical artist names and folder/path aliases in the song archive.
 * Used when normalizing `Artists/` folder variants (e.g. Noah_Cohn_Complete).
 */

export interface ArtistAliasEntry {
  /** URL slug for /archive/artists/<slug>/ */
  slug: string;
  /** Display name on artist pages and search results */
  name: string;
  /** Raw folder labels and path fragments that map to this artist */
  aliases: string[];
}

export const artistAliases: ArtistAliasEntry[] = [
  {
    slug: 'noah-cohn',
    name: 'Noah Cohn',
    aliases: [
      'Noah Cohn',
      'Noah Cohn Complete',
      'Noah Cohn Extended',
      'Noah_Cohn_Complete',
      'Noah_Cohn_Extended',
    ],
  },
  {
    slug: 'rotorkopf',
    name: 'Rotorkopf',
    aliases: ['Rotorkopf', 'Rotorkopf 2'],
  },
  {
    slug: 'cavey',
    name: 'Cavey',
    aliases: ['Cavey'],
  },
  {
    slug: 'dj-knightmare',
    name: 'DJ Knightmare',
    aliases: ['DJ Knightmare'],
  },
  {
    slug: 'dj-caspa',
    name: 'DJ Caspa',
    aliases: ['DJ Caspa'],
  },
  {
    slug: 'fanatic',
    name: 'Fanatic',
    aliases: ['Fanatic'],
  },
];

/** Monthly folder typos and display overrides (remote path segment → label). */
export const monthlyFolderDisplay: Record<string, string> = {
  JANRUARY: 'January',
  FEBRUARY: 'February',
  MARCH: 'March',
  APRIL: 'April',
  MAY: 'May',
  JUNE: 'June',
  JULY: 'July',
  AUGUST: 'August',
  SEPTEMBER: 'September',
  OCTOBER: 'October',
  'OCTOBER 2': 'October (vol. 2)',
  NOVEMBER: 'November',
  DECEMBER: 'December',
};
