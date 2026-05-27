/**
 * Archive Browser — client-side filtering, list rendering, and keyboard navigation.
 *
 * Imported as a module by songs.astro. All DOM queries are cached on init.
 * Filter results are computed once per change and reused for both grid + list views.
 */

import {
  songCollections,
  archiveBaseUrl,
  type SongSection,
  type SongCollectionKey,
} from '../data/song-collections';

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
  btnGrid: document.getElementById('btnGrid'),
  btnList: document.getElementById('btnList'),
  wrappers: () =>
    document.querySelectorAll<HTMLElement>('.collection-wrapper'),
  filterBtns: () =>
    document.querySelectorAll<HTMLElement>('.rb-filter-btn'),
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

  if (results.length === 0) {
    const row = document.createElement('tr');
    row.innerHTML = `<td colspan="3" class="rb-table-empty">No collections match your filters.</td>`;
    tbody.appendChild(row);
    return;
  }

  const frag = document.createDocumentFragment();

  results.forEach(({ sectionName, sectionDesc, sectionTags, folder }) => {
    const row = document.createElement('tr');

    // Name cell
    const nameDiv = document.createElement('div');
    nameDiv.className = 'cell-name';
    nameDiv.textContent = folder.name;

    const nameCell = document.createElement('td');
    nameCell.appendChild(nameDiv);

    if (sectionTags.length > 0) {
      const tagsSpan = document.createElement('span');
      tagsSpan.className = 'cell-tags';
      tagsSpan.textContent = sectionTags.join(' • ');
      nameCell.appendChild(tagsSpan);
    }

    // Section cell
    const sectionDiv = document.createElement('div');
    sectionDiv.textContent = sectionName;

    const descDiv = document.createElement('div');
    descDiv.className = 'cell-section-desc';
    descDiv.textContent = sectionDesc;

    const sectionCell = document.createElement('td');
    sectionCell.className = 'cell-section';
    sectionCell.appendChild(sectionDiv);
    sectionCell.appendChild(descDiv);

    // Action cell
    const link = document.createElement('a');
    link.href = `${archiveBaseUrl}/${folder.path}`;
    link.target = '_blank';
    link.rel = 'noopener noreferrer';
    link.className = 'open-link';
    link.textContent = 'Open ↗';

    const actionCell = document.createElement('td');
    actionCell.appendChild(link);

    row.appendChild(nameCell);
    row.appendChild(sectionCell);
    row.appendChild(actionCell);
    frag.appendChild(row);
  });

  tbody.appendChild(frag);
}

/* ------------------------------------------------------------------ */
//  Result Count
/* ------------------------------------------------------------------ */

function updateResultCount(sections: number, folders: number) {
  const el = dom.resultCount;
  if (!el) return;

  if (currentQuery === '' && currentFilter === 'all') {
    el.textContent = `Showing all ${sections} collections (${folders} folders)`;
  } else {
    el.textContent = `Showing ${sections} collections • ${folders} folders`;
  }
}

/* ------------------------------------------------------------------ */
//  Apply Filters (single entry point)
/* ------------------------------------------------------------------ */

function applyFilters() {
  const results = getFilteredData();
  const { visibleSections, visibleFolders } = updateGridView(results);
  renderListView(results);
  updateResultCount(visibleSections, visibleFolders);
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

  applyFilters();
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
      searchDebounce = window.setTimeout(() => applyFilters(), 80);
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
  bindEvents();
  applyFilters();
}

// Auto-init when imported directly (songs.astro usage)
if (typeof document !== 'undefined') {
  initArchiveBrowser();
}
