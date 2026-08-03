/**
 * Archive Browser — client-side filtering, list rendering, and keyboard navigation.
 *
 * Imported as a module by songs.astro. All DOM queries are cached on init.
 * Filter results are computed once per change and reused for both grid + list views.
 */

import {
  songCollections,
  type SongSection,
  type SongCollectionKey,
} from '../data/song-collections';
import collectionsMeta from '../data/collections-metadata.json';
import { normalizeBase } from '../lib/url';

interface CollectionMeta {
  description?: string;
  tags?: string[];
  curator?: string;
  lastUpdated?: string;
  totalItems?: number;
}

const sectionMeta = collectionsMeta as Record<SongCollectionKey, CollectionMeta>;

/* ------------------------------------------------------------------ */
//  Types
/* ------------------------------------------------------------------ */

interface FilterResult {
  sectionKey: SongCollectionKey;
  sectionName: string;
  sectionDesc: string;
  sectionTags: string[];
  folder: {
    name: string;
    path: string;
    count?: number;
  };
}

/* ------------------------------------------------------------------ */
//  State
/* ------------------------------------------------------------------ */

let currentQuery = '';
let currentFilter: SongCollectionKey | 'all' = 'all';
let searchDebounce = 0;
let currentView: 'grid' | 'list' = 'grid';
let currentSort: { col: 'name' | 'count'; dir: 'asc' | 'desc' } | null = null;

/* ------------------------------------------------------------------ */
//  DOM Cache
/* ------------------------------------------------------------------ */

const dom = {
  searchInput: document.getElementById('searchInput') as HTMLInputElement | null,
  resultCount: document.getElementById('resultCount'),
  listTableBody: document.getElementById('listTableBody'),
  gridView: document.getElementById('gridView'),
  listView: document.getElementById('listView'),
  gridEmptyState: document.getElementById('gridEmptyState'),
  listEmptyState: document.getElementById('listEmptyState'),
  btnGrid: document.getElementById('btnGrid'),
  btnList: document.getElementById('btnList'),
  wrappers: () =>
    document.querySelectorAll<HTMLElement>('.collection-wrapper'),
  filterBtns: () =>
    document.querySelectorAll<HTMLElement>('.rb-filter-btn'),
  sortBtns: () =>
    document.querySelectorAll<HTMLButtonElement>('.sort-btn'),
};

/* ------------------------------------------------------------------ */
//  Filtering
/* ------------------------------------------------------------------ */

function getFilteredData(): FilterResult[] {
  const query = currentQuery.toLowerCase().trim();
  const results: FilterResult[] = [];

  (Object.entries(songCollections) as [SongCollectionKey, SongSection][]).forEach(
    ([sectionKey, section]) => {
      if (currentFilter !== 'all' && currentFilter !== sectionKey) return;

      const sectionName = section.name.toLowerCase();
      const sectionDesc = (section.description ?? '').toLowerCase();
      const sectionTags = (section.tags ?? []).join(' ').toLowerCase();
      const sectionMatches =
        query === '' ||
        sectionName.includes(query) ||
        sectionDesc.includes(query) ||
        sectionTags.includes(query);

      section.folders.forEach((folder) => {
        const folderName = folder.name.toLowerCase();
        const folderMatches = folderName.includes(query);

        if (query === '' || sectionMatches || folderMatches) {
          results.push({
            sectionKey,
            sectionName: section.name,
            sectionDesc: section.description ?? '',
            sectionTags: section.tags ?? [],
            folder,
          });
        }
      });
    }
  );

  return results;
}

/* ------------------------------------------------------------------ */
//  Sorting
/* ------------------------------------------------------------------ */

function getSortedData(results: FilterResult[]): FilterResult[] {
  if (!currentSort) return results;

  return [...results].sort((a, b) => {
    let cmp = 0;
    if (currentSort!.col === 'name') {
      cmp = a.folder.name.localeCompare(b.folder.name);
    } else if (currentSort!.col === 'count') {
      cmp = (a.folder.count ?? 0) - (b.folder.count ?? 0);
    }
    return currentSort!.dir === 'asc' ? cmp : -cmp;
  });
}

function updateSortIndicators() {
  dom.sortBtns().forEach((btn) => {
    const col = btn.getAttribute('data-sort') as 'name' | 'count';
    const indicator = btn.querySelector('.sort-indicator');
    if (currentSort && currentSort.col === col) {
      btn.setAttribute('data-dir', currentSort.dir);
      if (indicator) indicator.textContent = currentSort.dir === 'asc' ? '↑' : '↓';
    } else {
      btn.removeAttribute('data-dir');
      if (indicator) indicator.textContent = '↕';
    }
  });
}

/* ------------------------------------------------------------------ */
//  Grid View
/* ------------------------------------------------------------------ */

function updateGridView(results: FilterResult[]) {
  const wrappers = dom.wrappers();

  // Build a set of visible section keys + folder names for fast lookup
  const visibleSections = new Set<SongCollectionKey>();
  const visibleFolders = new Set<string>();

  results.forEach((r) => {
    visibleSections.add(r.sectionKey);
    visibleFolders.add(r.folder.name);
  });

  wrappers.forEach((wrapper) => {
    const sectionKey = wrapper.getAttribute('data-section-key') as SongCollectionKey | null;
    if (!sectionKey) return;

    const sectionVisible = visibleSections.has(sectionKey);
    wrapper.classList.toggle('is-hidden', !sectionVisible);

    if (!sectionVisible) return;

    wrapper.querySelectorAll<HTMLElement>('.folder-item').forEach((item) => {
      const nameEl = item.querySelector('.folder-name');
      const folderName = nameEl?.textContent ?? '';
      item.classList.toggle('is-hidden', !visibleFolders.has(folderName));
    });
  });

  // Show/hide grid empty state
  if (dom.gridEmptyState) {
    dom.gridEmptyState.classList.toggle('is-hidden', visibleSections.size > 0);
  }

  return { visibleSections: visibleSections.size, visibleFolders: visibleFolders.size };
}

/* ------------------------------------------------------------------ */
//  List View
/* ------------------------------------------------------------------ */

function renderListView(results: FilterResult[]) {
  const tbody = dom.listTableBody;
  if (!tbody) return;

  tbody.innerHTML = '';
  if (results.length === 0) return;

  const frag = document.createDocumentFragment();

  results.forEach(({ sectionKey, sectionName, sectionDesc, folder }) => {
    const meta = sectionMeta[sectionKey];
    const tags = meta?.tags ?? [];
    const curator = meta?.curator ?? '—';
    const updated = meta?.lastUpdated ?? '—';
    const count = folder.count ?? 0;

    const row = document.createElement('tr');

    // Collection cell
    const nameDiv = document.createElement('div');
    nameDiv.className = 'cell-name';
    nameDiv.textContent = folder.name;

    const nameCell = document.createElement('td');
    nameCell.appendChild(nameDiv);

    // Section cell
    const sectionDiv = document.createElement('div');
    sectionDiv.className = 'cell-section-name';
    sectionDiv.textContent = sectionName;

    const descDiv = document.createElement('div');
    descDiv.className = 'cell-section-desc';
    descDiv.textContent = sectionDesc;

    const sectionCell = document.createElement('td');
    sectionCell.className = 'cell-section';
    sectionCell.appendChild(sectionDiv);
    sectionCell.appendChild(descDiv);

    // Files cell
    const countCell = document.createElement('td');
    countCell.className = 'cell-count';
    countCell.textContent = count > 0 ? count.toString() : '—';

    // Tags cell
    const tagsCell = document.createElement('td');
    tagsCell.className = 'cell-tags';
    if (tags.length > 0) {
      tags.slice(0, 3).forEach((tag) => {
        const span = document.createElement('span');
        span.className = 'list-tag';
        span.textContent = tag;
        tagsCell.appendChild(span);
      });
    } else {
      tagsCell.textContent = '—';
    }

    // Curator cell
    const curatorCell = document.createElement('td');
    curatorCell.className = 'cell-curator';
    curatorCell.textContent = curator;

    // Updated cell
    const updatedCell = document.createElement('td');
    updatedCell.className = 'cell-updated';
    updatedCell.textContent = updated;

    // Action cell
    const folderSlug = folder.path.split('/').map(encodeURIComponent).join('/');
    const link = document.createElement('a');
    link.href = `${normalizeBase(import.meta.env.BASE_URL)}/archive/songs/${folderSlug}/`;
    link.className = 'open-link';
    link.textContent = 'Browse →';

    const actionCell = document.createElement('td');
    actionCell.className = 'cell-action';
    actionCell.appendChild(link);

    row.appendChild(nameCell);
    row.appendChild(sectionCell);
    row.appendChild(countCell);
    row.appendChild(tagsCell);
    row.appendChild(curatorCell);
    row.appendChild(updatedCell);
    row.appendChild(actionCell);
    frag.appendChild(row);
  });

  tbody.appendChild(frag);
}

/* ------------------------------------------------------------------ */
//  Result Count
/* ------------------------------------------------------------------ */

function updateResultCount(sections: number, folders: number, rows: number) {
  const el = dom.resultCount;
  if (!el) return;

  if (currentQuery === '' && currentFilter === 'all' && currentSort === null) {
    el.textContent = `Showing all ${sections} song collections (${folders} folders)`;
  } else {
    el.textContent = `Showing ${sections} collections • ${folders} matching folders • ${rows} list entries`;
  }
}

/* ------------------------------------------------------------------ */
//  Apply Filters (single entry point)
/* ------------------------------------------------------------------ */

function applyFilters() {
  const results = getSortedData(getFilteredData());
  const { visibleSections, visibleFolders } = updateGridView(results);
  renderListView(results);
  updateResultCount(visibleSections, visibleFolders, results.length);
  if (dom.listEmptyState) {
    dom.listEmptyState.classList.toggle('is-hidden', results.length > 0);
  }
}

function syncUrl() {
  const params = new URLSearchParams(window.location.search);

  if (currentQuery.trim()) {
    params.set('q', currentQuery.trim());
  } else {
    params.delete('q');
  }

  if (currentFilter !== 'all') {
    params.set('filter', currentFilter);
  } else {
    params.delete('filter');
  }

  if (currentView !== 'grid') {
    params.set('view', currentView);
  } else {
    params.delete('view');
  }

  const search = params.toString();
  const nextUrl = `${window.location.pathname}${search ? `?${search}` : ''}${window.location.hash}`;
  window.history.replaceState({}, '', nextUrl);
}

/* ------------------------------------------------------------------ */
//  Clear All
/* ------------------------------------------------------------------ */

function clearAllFilters() {
  currentQuery = '';
  currentFilter = 'all';

  if (dom.searchInput) dom.searchInput.value = '';

  dom.filterBtns().forEach((btn) => {
    const isAll = btn.getAttribute('data-filter') === 'all';
    btn.classList.toggle('active', isAll);
    btn.setAttribute('aria-pressed', String(isAll));
  });

  applyFilters();
  syncUrl();
}

/* ------------------------------------------------------------------ */
//  View Switching
/* ------------------------------------------------------------------ */

function switchView(view: 'grid' | 'list') {
  const grid = dom.gridView;
  const list = dom.listView;
  const btnGrid = dom.btnGrid;
  const btnList = dom.btnList;

  if (!grid || !list) return;

  if (view === 'grid') {
    grid.classList.remove('is-hidden');
    list.classList.add('is-hidden');
    btnGrid?.classList.add('active');
    btnGrid?.setAttribute('aria-pressed', 'true');
    btnList?.classList.remove('active');
    btnList?.setAttribute('aria-pressed', 'false');
  } else {
    grid.classList.add('is-hidden');
    list.classList.remove('is-hidden');
    btnList?.classList.add('active');
    btnList?.setAttribute('aria-pressed', 'true');
    btnGrid?.classList.remove('active');
    btnGrid?.setAttribute('aria-pressed', 'false');
  }

  currentView = view;

  if (typeof localStorage !== 'undefined') {
    localStorage.setItem('archiveView', view);
  }

  applyFilters();
  syncUrl();
}

function applyInitialStateFromUrl() {
  const params = new URLSearchParams(window.location.search);
  const query = params.get('q');
  const filter = params.get('filter');
  const view = params.get('view');

  if (query) {
    currentQuery = query;
    if (dom.searchInput) {
      dom.searchInput.value = query;
    }
  }

  if (filter && ['featured', 'bySource', 'artists', 'monthly', 'other'].includes(filter)) {
    currentFilter = filter as SongCollectionKey;
    dom.filterBtns().forEach((btn) => {
      const isActive = btn.getAttribute('data-filter') === filter;
      btn.classList.toggle('active', isActive);
      btn.setAttribute('aria-pressed', String(isActive));
    });
  }

  if (view === 'list' || view === 'grid') {
    currentView = view;
  } else if (typeof localStorage !== 'undefined') {
    const saved = localStorage.getItem('archiveView');
    if (saved === 'list' || saved === 'grid') {
      currentView = saved;
    }
  }
}

/* ------------------------------------------------------------------ */
//  Event Listeners
/* ------------------------------------------------------------------ */

function bindEvents() {
  // Search input (debounced)
  if (dom.searchInput) {
    dom.searchInput.addEventListener('input', (e) => {
      currentQuery = (e.target as HTMLInputElement).value;
      window.clearTimeout(searchDebounce);
      searchDebounce = window.setTimeout(() => {
        applyFilters();
        syncUrl();
      }, 80);
    });
  }

  // Filter buttons
  dom.filterBtns().forEach((btn) => {
    btn.addEventListener('click', () => {
      const filter = (btn.getAttribute('data-filter') ?? 'all') as SongCollectionKey | 'all';
      currentFilter = filter;

      dom.filterBtns().forEach((b) => {
        b.classList.remove('active');
        b.setAttribute('aria-pressed', 'false');
      });
      btn.classList.add('active');
      btn.setAttribute('aria-pressed', 'true');

      applyFilters();
      syncUrl();
    });
  });

  // List-view column sorting
  dom.sortBtns().forEach((btn) => {
    btn.addEventListener('click', () => {
      const col = btn.getAttribute('data-sort') as 'name' | 'count';
      if (!col) return;

      if (currentSort?.col === col) {
        currentSort.dir = currentSort.dir === 'asc' ? 'desc' : 'asc';
      } else {
        currentSort = { col, dir: 'asc' };
      }

      updateSortIndicators();
      applyFilters();
    });
  });

  // Keyboard shortcut: / to focus search
  document.addEventListener('keydown', (e) => {
    if (
      e.key === '/' &&
      document.activeElement?.tagName !== 'INPUT' &&
      document.activeElement?.tagName !== 'TEXTAREA'
    ) {
      e.preventDefault();
      dom.searchInput?.focus();
    }
  });

  // Arrow key navigation within collection cards
  document.addEventListener('keydown', (e) => {
    const active = document.activeElement;
    if (!active || !active.classList.contains('folder-item')) return;

    const wrapper = active.closest('.collection-wrapper');
    if (!wrapper) return;

    const items = Array.from(
      wrapper.querySelectorAll<HTMLElement>('.folder-item')
    ).filter((item) => item.style.display !== 'none');

    const idx = items.indexOf(active as HTMLElement);
    if (idx === -1) return;

    if (e.key === 'ArrowDown') {
      e.preventDefault();
      items[idx + 1]?.focus();
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      items[idx - 1]?.focus();
    } else if (e.key === 'ArrowRight') {
      e.preventDefault();
      const allWrappers = Array.from(
        document.querySelectorAll<HTMLElement>('.collection-wrapper')
      ).filter((w) => !w.classList.contains('is-hidden'));
      const wrapperIdx = allWrappers.indexOf(wrapper as HTMLElement);
      allWrappers[wrapperIdx + 1]
        ?.querySelector<HTMLElement>('.folder-item')
        ?.focus();
    } else if (e.key === 'ArrowLeft') {
      e.preventDefault();
      const allWrappers = Array.from(
        document.querySelectorAll<HTMLElement>('.collection-wrapper')
      ).filter((w) => !w.classList.contains('is-hidden'));
      const wrapperIdx = allWrappers.indexOf(wrapper as HTMLElement);
      allWrappers[wrapperIdx - 1]
        ?.querySelector<HTMLElement>('.folder-item')
        ?.focus();
    }
  });
}

/* ------------------------------------------------------------------ */
//  Public API (exposed for inline onclick handlers in HTML)
/* ------------------------------------------------------------------ */

// Expose on window so inline onclick="..." can still reach them
// after Astro bundles this as a module.
declare global {
  interface Window {
    clearAllFilters: () => void;
    switchView: (view: 'grid' | 'list') => void;
  }
}

window.clearAllFilters = clearAllFilters;
window.switchView = switchView;

/* ------------------------------------------------------------------ */
//  Init
/* ------------------------------------------------------------------ */

export function initArchiveBrowser(): void {
  applyInitialStateFromUrl();
  bindEvents();
  if (currentView === 'list') {
    switchView('list');
  } else {
    applyFilters();
  }
}

// Auto-init when imported directly (songs.astro usage)
if (typeof document !== 'undefined') {
  initArchiveBrowser();
}
