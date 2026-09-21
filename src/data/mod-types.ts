/**
 * Shapes of the two mod JSON data files, so pages can consume them without
 * `any`.
 *
 * - `mods-metadata.json` — the curated subset with titles, authors and tags.
 * - `mods-full-index.json` — every `.rbm` in the archive, documented or not.
 */

import modsMetadata from './mods-metadata.json';
import modsFullIndex from './mods-full-index.json';

/** One curated entry from `mods-metadata.json`. */
export interface DocumentedMod {
  filename: string;
  title: string;
  author: string;
  /** Release year, or null when it could not be established. */
  year: number | null;
  description?: string;
  tags: string[];
}

/** One entry from `mods-full-index.json` (every hosted `.rbm`). */
export interface IndexedMod {
  filename: string;
  /** Human-readable file size, e.g. `"8.1M"`. */
  size: string;
  /** True when a matching `mods-metadata.json` entry exists. */
  hasMeta: boolean;
  title?: string;
  author?: string;
  tags?: string[];
}

/** Curated mods, typed. */
export const documentedMods = modsMetadata.mods as DocumentedMod[];

/** Every indexed `.rbm`, typed. */
export const indexedMods = modsFullIndex.mods as IndexedMod[];
