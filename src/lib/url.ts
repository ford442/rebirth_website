/**
 * Normalise Astro's `import.meta.env.BASE_URL` into a consistent
 * leading-slash, no-trailing-slash path segment.
 *
 * Examples:
 *   "/rebirth_website"  → "/rebirth_website"
 *   "/rebirth_website/" → "/rebirth_website"
 *   "/"                 → ""
 *   ""                  → ""
 */
export function normalizeBase(value: string | undefined): string {
  const rawBase = value ?? '';
  if (rawBase === '/' || rawBase === '') return '';
  const trimmed = rawBase.replace(/\/+$/, '');
  return trimmed.startsWith('/') ? trimmed : `/${trimmed}`;
}
