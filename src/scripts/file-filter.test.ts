// @vitest-environment happy-dom
import { beforeEach, describe, expect, it } from 'vitest';
import { filterByFilename } from './file-filter';

function mount() {
  document.body.innerHTML = `
    <span id="count"></span>
    <p id="empty" class="is-hidden"></p>
    <div class="group" id="g1">
      <div class="item" data-filename="alpha.rbs"></div>
      <div class="item" data-filename="alpine.rbs"></div>
    </div>
    <div class="group" id="g2">
      <div class="item" data-filename="beta.rbs"></div>
    </div>
    <div class="item" data-filename="no-group.rbs"></div>
  `;
  const $ = (selector: string) => document.querySelector<HTMLElement>(selector)!;
  return {
    items: document.querySelectorAll<HTMLElement>('.item'),
    groups: document.querySelectorAll<HTMLElement>('.group'),
    countLabel: $('#count'),
    emptyState: $('#empty'),
    g1: $('#g1'),
    g2: $('#g2'),
  };
}

const shown = (items: ArrayLike<HTMLElement>) =>
  Array.from(items)
    .filter((el) => el.style.display !== 'none')
    .map((el) => el.dataset.filename);

describe('filterByFilename', () => {
  let ui: ReturnType<typeof mount>;
  beforeEach(() => {
    ui = mount();
  });

  it('shows everything and a plain count when the term is empty', () => {
    const visible = filterByFilename({ ...ui, term: '' });
    expect(visible).toHaveLength(4);
    expect(ui.countLabel.textContent).toBe('4 files');
    expect(ui.emptyState.classList.contains('is-hidden')).toBe(true);
  });

  it('keeps the items whose name contains the term, and counts them', () => {
    const visible = filterByFilename({ ...ui, term: 'alp' });
    expect(visible.map((el) => el.dataset.filename)).toEqual(['alpha.rbs', 'alpine.rbs']);
    expect(shown(ui.items)).toEqual(['alpha.rbs', 'alpine.rbs']);
    expect(ui.countLabel.textContent).toBe('2 of 4 files');
  });

  it('matches case-insensitively, since file names are stored lower-case', () => {
    expect(filterByFilename({ ...ui, term: 'BETA' }).map((el) => el.dataset.filename)).toEqual([
      'beta.rbs',
    ]);
  });

  it('matches anywhere in the name, extension included', () => {
    expect(filterByFilename({ ...ui, term: 'a.rbs' })).toHaveLength(2); // alpha, beta
    expect(filterByFilename({ ...ui, term: '.rbs' })).toHaveLength(4);
  });

  it('hides a group once none of its items match, and brings it back', () => {
    filterByFilename({ ...ui, term: 'beta' });
    expect(ui.g1.style.display).toBe('none');
    expect(ui.g2.style.display).toBe('');

    filterByFilename({ ...ui, term: '' });
    expect(ui.g1.style.display).toBe('');
    expect(ui.g2.style.display).toBe('');
  });

  it('reveals the empty state only when nothing matches', () => {
    filterByFilename({ ...ui, term: 'zzz' });
    expect(ui.emptyState.classList.contains('is-hidden')).toBe(false);
    expect(ui.countLabel.textContent).toBe('0 of 4 files');
    expect(ui.g1.style.display).toBe('none');
    expect(ui.g2.style.display).toBe('none');

    filterByFilename({ ...ui, term: 'beta' });
    expect(ui.emptyState.classList.contains('is-hidden')).toBe(true);
  });

  it('treats an item with no data-filename as matching only the empty term', () => {
    const bare = document.createElement('div');
    bare.className = 'item';
    document.body.appendChild(bare);
    const items = document.querySelectorAll<HTMLElement>('.item');

    filterByFilename({ items, term: '' });
    expect(bare.style.display).toBe('');
    filterByFilename({ items, term: 'a' });
    expect(bare.style.display).toBe('none');
  });

  it('works with just items — label, empty state and groups are all optional', () => {
    expect(() => filterByFilename({ items: ui.items, term: 'alp' })).not.toThrow();
    expect(shown(ui.items)).toEqual(['alpha.rbs', 'alpine.rbs']);
  });

  it('accepts a plain array of rows (the folder table passes one)', () => {
    const rows = Array.from(ui.items);
    expect(filterByFilename({ items: rows, term: 'no-group' })).toEqual([rows[3]]);
  });
});
