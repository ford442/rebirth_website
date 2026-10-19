import { describe, expect, it } from 'vitest';
import { classifyDroppedFile, fileSlug } from './file-names';

describe('classifyDroppedFile', () => {
  it.each([
    ['song.rbs', 'rbs'],
    ['kit.rbm', 'rbm'],
    // 90s Windows spelled everything in capitals; the drop was silently ignored.
    ['SONG.RBS', 'rbs'],
    ['MOD.RBM', 'rbm'],
    ['Mixed.Rbs', 'rbs'],
    ['Mixed.rBm', 'rbm'],
    ['my song v2.final.rbs', 'rbs'],
    ['.rbs', 'rbs'],
  ])('%s → %s', (name, kind) => {
    expect(classifyDroppedFile(name)).toBe(kind);
  });

  it.each([
    ['notes.txt'],
    ['song.mid'],
    ['song.rbs.txt'],
    ['song.rbsx'],
    ['rbs'],
    ['song.rbs '],
    [''],
  ])('%j is neither', (name) => {
    expect(classifyDroppedFile(name)).toBeNull();
  });
});

describe('fileSlug', () => {
  it.each([
    ['Acid Rain', 'acid-rain'],
    ['  Padded  Title  ', 'padded-title'],
    ['Låt №1 (Remix) — v2!', 'l-t-1-remix-v2'],
    ['already-a-slug', 'already-a-slug'],
    ['UPPER_snake_Case', 'upper-snake-case'],
    ['---edges---', 'edges'],
    ['a   b', 'a-b'],
    ['123', '123'],
  ])('%j → %j', (title, slug) => {
    expect(fileSlug(title)).toBe(slug);
  });

  it.each([[''], ['***'], ['—'], ['日本語'], [null], [undefined]])('falls back for %j', (title) => {
    expect(fileSlug(title)).toBe('rebirth-song');
  });

  it('takes a custom fallback', () => {
    expect(fileSlug('', 'untitled')).toBe('untitled');
    expect(fileSlug('!!!', 'untitled')).toBe('untitled');
  });

  it('caps the length at 48 characters', () => {
    expect(fileSlug('a'.repeat(100))).toBe('a'.repeat(48));
  });

  it('never ends on a dash, even when the cut lands on one', () => {
    // 47 letters, a separator, then more: the 48-character cut keeps the dash.
    const slug = fileSlug(`${'a'.repeat(47)} ${'b'.repeat(10)}`);
    expect(slug).toBe('a'.repeat(47));
    expect(slug).not.toMatch(/-$/);
  });

  it('produces only filesystem-safe characters', () => {
    expect(fileSlug('a/b\\c:d*e?f"g<h>i|j')).toMatch(/^[a-z0-9-]+$/);
  });
});
