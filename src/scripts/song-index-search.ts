/**
 * Client-side song index search powered by MiniSearch.
 * Loads the compact index from /data/songs-search-index.json on demand.
 */
import MiniSearch from 'minisearch';
import { normalizeBase } from '../lib/url';

export interface SearchSongRecord {
  id: number;
  filename: string;
  path: string;
  url: string;
  title: string;
  collection: string | null;
  subcollection: string | null;
  subcollectionDisplay: string | null;
  artistCanonical: string | null;
  artistSlug: string | null;
  metaAuthor: string | null;
  modName: string | null;
  bpm: number | null;
  fileSize: string;
  sourceVersion: string | null;
  hasModDependency: boolean;
  localPath: string | null;
  linkStatus: string | null;
  sha256: string | null;
}

export interface SearchFacets {
  collections: Array<{ name: string; count: number }>;
  sourceVersions: Array<{ name: string; count: number }>;
  modDependentCount: number;
  bpmRange: { min: number | null; max: number | null; withBpm: number };
}

export interface SongSearchIndex {
  meta: { totalSongs: number };
  facets: SearchFacets;
  songs: SearchSongRecord[];
}

export interface SongSearchFilters {
  query: string;
  collection: string;
  sourceVersion: string;
  modOnly: boolean;
  bpmMin: number | null;
  bpmMax: number | null;
}

const DEFAULT_FILTERS: SongSearchFilters = {
  query: '',
  collection: '',
  sourceVersion: '',
  modOnly: false,
  bpmMin: null,
  bpmMax: null,
};

let loadPromise: Promise<SongSearchIndex> | null = null;
let miniSearch: MiniSearch<SearchSongRecord> | null = null;
let indexData: SongSearchIndex | null = null;

function searchDataUrl(): string {
  const base = normalizeBase(
    document.querySelector<HTMLMetaElement>('meta[name="base-url"]')?.content ?? '/rebirth_website'
  );
  const prefix = base ? `${base}/` : '/';
  return `${prefix}data/songs-search-index.json`;
}

export async function loadSongSearchIndex(): Promise<SongSearchIndex> {
  if (indexData) return indexData;
  if (!loadPromise) {
    loadPromise = fetch(searchDataUrl())
      .then((res) => {
        if (!res.ok) throw new Error(`Failed to load song search index (${res.status})`);
        return res.json() as Promise<SongSearchIndex>;
      })
      .then((data) => {
        indexData = data;
        miniSearch = new MiniSearch({
          fields: [
            'title',
            'filename',
            'artistCanonical',
            'metaAuthor',
            'modName',
            'collection',
            'subcollectionDisplay',
            'path',
          ],
          storeFields: [
            'id',
            'filename',
            'path',
            'url',
            'title',
            'collection',
            'subcollectionDisplay',
            'artistCanonical',
            'artistSlug',
            'metaAuthor',
            'modName',
            'bpm',
            'fileSize',
            'sourceVersion',
            'hasModDependency',
            'localPath',
            'linkStatus',
            'sha256',
          ],
          searchOptions: {
            prefix: true,
            fuzzy: 0.15,
            boost: { title: 2, artistCanonical: 1.5, metaAuthor: 1.3, filename: 1.2 },
          },
        });
        miniSearch.addAll(data.songs);
        return data;
      });
  }
  return loadPromise;
}

function passesFacets(song: SearchSongRecord, filters: SongSearchFilters): boolean {
  if (filters.collection && song.collection !== filters.collection) return false;
  if (filters.sourceVersion && song.sourceVersion !== filters.sourceVersion) return false;
  if (filters.modOnly && !song.hasModDependency) return false;
  if (filters.bpmMin != null && (song.bpm == null || song.bpm < filters.bpmMin)) return false;
  if (filters.bpmMax != null && (song.bpm == null || song.bpm > filters.bpmMax)) return false;
  return true;
}

function resolveHits(ids: Array<{ id: number }>): SearchSongRecord[] {
  if (!indexData) return [];
  const byId = new Map(indexData.songs.map((song) => [song.id, song as SearchSongRecord]));
  return ids
    .map((hit) => byId.get(hit.id))
    .filter((song): song is SearchSongRecord => Boolean(song));
}

export function searchSongs(
  filters: Partial<SongSearchFilters> = {},
  limit = 150
): SearchSongRecord[] {
  if (!miniSearch || !indexData) return [];
  const f: SongSearchFilters = { ...DEFAULT_FILTERS, ...filters };
  const term = f.query.trim();

  const candidates = term
    ? resolveHits(miniSearch.search(term))
    : (indexData.songs as SearchSongRecord[]);

  return candidates.filter((song) => passesFacets(song, f)).slice(0, limit);
}

export function countSearchResults(filters: Partial<SongSearchFilters> = {}): number {
  if (!miniSearch || !indexData) return 0;
  const f: SongSearchFilters = { ...DEFAULT_FILTERS, ...filters };
  const term = f.query.trim();

  const candidates = term
    ? resolveHits(miniSearch.search(term))
    : (indexData.songs as SearchSongRecord[]);

  return candidates.filter((song) => passesFacets(song, f)).length;
}

export function getSearchFacets(): SearchFacets | null {
  return indexData?.facets ?? null;
}

export function getTotalIndexed(): number {
  return indexData?.meta.totalSongs ?? 0;
}
