/**
 * Mount the complete song index search UI (MiniSearch + facets).
 */
import { previewSong } from '../lib/player-events';
import { normalizeBase } from '../lib/url';
import {
  countSearchResults,
  getSearchFacets,
  getTotalIndexed,
  loadSongSearchIndex,
  searchSongs,
  type SearchSongRecord,
} from './song-index-search';

const RESULT_LIMIT = 150;

function getBase(): string {
  const meta = document.querySelector<HTMLMetaElement>('meta[name="base-url"]');
  return normalizeBase(meta?.content ?? '/rebirth_website');
}

function escapeHtml(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function renderResultRow(song: SearchSongRecord, base: string): string {
  const artistLink =
    song.artistSlug && song.artistCanonical
      ? `<a class="result-artist" href="${base}/archive/artists/${song.artistSlug}/">${escapeHtml(song.artistCanonical)}</a>`
      : song.artistCanonical
        ? `<span class="result-artist">${escapeHtml(song.artistCanonical)}</span>`
        : '';

  const sub = song.subcollectionDisplay ?? song.subcollection ?? '';
  const metaParts = [song.collection, sub].filter((part) => Boolean(part)).map((part) => escapeHtml(String(part)));
  const bpm = song.bpm ? `<span class="result-bpm">${song.bpm} BPM</span>` : '';
  const modBadge = song.hasModDependency ? '<span class="result-mod" title="Best heard with a specific .rbm mod loaded">MOD REQUIRED</span>' : '';

  return `
    <article class="song-result" data-id="${song.id}">
      <div class="song-result__main">
        <h3 class="song-result__title" title="${escapeHtml(song.filename)}">${escapeHtml(song.title)}</h3>
        <p class="song-result__meta">${metaParts.join(' · ')} ${artistLink ? `· ${artistLink}` : ''} ${bpm} ${modBadge}</p>
      </div>
      <div class="song-result__actions">
        <button type="button" class="preview-btn song-result__play" data-preview-src="${escapeHtml(song.url)}" data-preview-label="${escapeHtml(song.title)}" data-preview-bpm="${song.bpm ?? ''}" aria-label="Preview ${escapeHtml(song.filename)}">
          ▶
        </button>
        <a href="${escapeHtml(song.url)}" target="_blank" rel="noopener noreferrer" class="song-result__download" aria-label="Download ${escapeHtml(song.filename)}">↓</a>
      </div>
    </article>
  `;
}

function readFilters(root: HTMLElement) {
  const query = (root.querySelector<HTMLInputElement>('#songSearch')?.value ?? '').trim();
  const collection = root.querySelector<HTMLSelectElement>('#facetCollection')?.value ?? '';
  const sourceVersion = root.querySelector<HTMLSelectElement>('#facetSource')?.value ?? '';
  const modOnly = root.querySelector<HTMLInputElement>('#facetModOnly')?.checked ?? false;
  const bpmMinRaw = root.querySelector<HTMLInputElement>('#facetBpmMin')?.value ?? '';
  const bpmMaxRaw = root.querySelector<HTMLInputElement>('#facetBpmMax')?.value ?? '';
  const bpmMin = bpmMinRaw ? Number(bpmMinRaw) : null;
  const bpmMax = bpmMaxRaw ? Number(bpmMaxRaw) : null;

  return { query, collection, sourceVersion, modOnly, bpmMin, bpmMax };
}

function wirePreviewButtons(root: HTMLElement, base: string) {
  root.querySelectorAll<HTMLButtonElement>('.song-result__play').forEach((button) => {
    button.addEventListener('click', () => {
      const src = button.dataset.previewSrc;
      if (!src) return;
      previewSong(base, {
        src,
        label: button.dataset.previewLabel ?? 'archive preview',
        bpm: button.dataset.previewBpm ? Number(button.dataset.previewBpm) : undefined,
      });
    });
  });
}

function renderResults(root: HTMLElement, base: string) {
  const filters = readFilters(root);
  const total = countSearchResults(filters);
  const results = searchSongs(filters, RESULT_LIMIT);
  const list = root.querySelector('#songResults');
  const count = root.querySelector('#songCount');
  const empty = root.querySelector('#songEmpty');
  const hint = root.querySelector('#songSearchHint');

  if (count) {
    const indexed = getTotalIndexed();
    if (total > results.length) {
      count.textContent = `${results.length} shown · ${total.toLocaleString()} matches · ${indexed.toLocaleString()} indexed`;
    } else {
      count.textContent = `${total.toLocaleString()} of ${indexed.toLocaleString()} files`;
    }
  }

  if (list) {
    list.innerHTML = results.map((song) => renderResultRow(song, base)).join('');
    wirePreviewButtons(root, base);
  }

  if (empty) {
    empty.classList.toggle('is-hidden', results.length > 0);
  }

  if (hint) {
    const hasFilter = Boolean(
      filters.query || filters.collection || filters.sourceVersion || filters.modOnly || filters.bpmMin || filters.bpmMax
    );
    hint.classList.toggle('is-hidden', hasFilter || results.length > 0);
  }
}

function populateFacets(root: HTMLElement) {
  const facets = getSearchFacets();
  if (!facets) return;

  const collectionSelect = root.querySelector<HTMLSelectElement>('#facetCollection');
  if (collectionSelect) {
    facets.collections.forEach(({ name, count }) => {
      const opt = document.createElement('option');
      opt.value = name;
      opt.textContent = `${name} (${count})`;
      collectionSelect.appendChild(opt);
    });
  }

  const sourceSelect = root.querySelector<HTMLSelectElement>('#facetSource');
  if (sourceSelect) {
    facets.sourceVersions.forEach(({ name, count }) => {
      const opt = document.createElement('option');
      opt.value = name;
      opt.textContent = `${name} (${count})`;
      sourceSelect.appendChild(opt);
    });
  }

  const modLabel = root.querySelector('#facetModLabel');
  if (modLabel) {
    modLabel.textContent = `MOD-dependent songs (${facets.modDependentCount})`;
  }
}

export async function mountSongArchiveSearch(rootId = 'songSearchRoot'): Promise<void> {
  const root = document.getElementById(rootId);
  if (!root) return;

  const base = getBase();
  root.classList.add('is-loading');

  try {
    await loadSongSearchIndex();
    populateFacets(root);
    renderResults(root, base);

    const rerender = () => renderResults(root, base);
    root.querySelector('#songSearch')?.addEventListener('input', rerender);
    root.querySelector('#facetCollection')?.addEventListener('change', rerender);
    root.querySelector('#facetSource')?.addEventListener('change', rerender);
    root.querySelector('#facetModOnly')?.addEventListener('change', rerender);
    root.querySelector('#facetBpmMin')?.addEventListener('input', rerender);
    root.querySelector('#facetBpmMax')?.addEventListener('input', rerender);
    root.querySelector('#clearSongFilters')?.addEventListener('click', () => {
      (root.querySelector('#songSearch') as HTMLInputElement).value = '';
      (root.querySelector('#facetCollection') as HTMLSelectElement).value = '';
      (root.querySelector('#facetSource') as HTMLSelectElement).value = '';
      (root.querySelector('#facetModOnly') as HTMLInputElement).checked = false;
      (root.querySelector('#facetBpmMin') as HTMLInputElement).value = '';
      (root.querySelector('#facetBpmMax') as HTMLInputElement).value = '';
      rerender();
    });
  } finally {
    root.classList.remove('is-loading');
  }
}

mountSongArchiveSearch();
