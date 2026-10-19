/**
 * Filename filter shared by the archive file lists (the raw `.rbm` index on
 * /archive/mods and the folder tables on /archive/songs/<folder>).
 *
 * Each item carries its lower-case file name in `data-filename`; a search term
 * keeps the items whose name contains it. The helper owns everything that has
 * to stay in step when the term changes — which items show, the "N of M
 * files" label, the empty state, and any group wrapper left with nothing in
 * it — so the pages cannot drift apart on it.
 */

export interface FileFilterOptions {
  /** What the user typed. Matched case-insensitively against `data-filename`. */
  term: string;
  /** Every filterable item, whether or not it is currently shown. */
  items: ArrayLike<HTMLElement>;
  /** Shows "M files", or "N of M files" while a term is active. */
  countLabel?: HTMLElement | null;
  /** Revealed (`is-hidden` removed) only when nothing matches. */
  emptyState?: HTMLElement | null;
  /** Wrappers around runs of items, hidden when none of their items match. */
  groups?: ArrayLike<HTMLElement>;
}

/** Apply the filter, and return the items still showing. */
export function filterByFilename(options: FileFilterOptions): HTMLElement[] {
  const { items, countLabel, emptyState, groups } = options;
  const term = options.term.toLowerCase();
  const all = Array.from(items);

  for (const item of all) {
    const filename = item.getAttribute('data-filename') ?? '';
    item.style.display = !term || filename.includes(term) ? '' : 'none';
  }
  const visible = all.filter((item) => item.style.display !== 'none');

  if (groups) {
    for (const group of Array.from(groups)) {
      const hasVisible = Array.from(group.querySelectorAll<HTMLElement>('[data-filename]')).some(
        (item) => item.style.display !== 'none'
      );
      group.style.display = hasVisible ? '' : 'none';
    }
  }

  if (countLabel) {
    countLabel.textContent = term
      ? `${visible.length} of ${all.length} files`
      : `${all.length} files`;
  }
  emptyState?.classList.toggle('is-hidden', visible.length > 0);

  return visible;
}
