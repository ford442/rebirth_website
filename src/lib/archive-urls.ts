/**
 * Canonical URLs for externally hosted archive assets.
 * Always use HTTPS — test.1ink.us redirects HTTP but some browsers
 * block or warn on mixed-content navigations from the GitHub Pages site.
 */
export const RBS_ARCHIVE_BASE_URL = 'https://test.1ink.us/rb338/archive/rbs-songs';

/** Build a direct .rbs download URL from an archive-relative path. */
export function rbsDownloadUrl(relativePath: string): string {
  const encoded = relativePath
    .split('/')
    .map((segment) => encodeURIComponent(segment))
    .join('/');
  return `${RBS_ARCHIVE_BASE_URL}/${encoded}`;
}

/** Upgrade legacy http archive links to https. */
export function normalizeArchiveUrl(url: string): string {
  return url.replace(/^http:\/\/test\.1ink\.us\//, 'https://test.1ink.us/');
}
