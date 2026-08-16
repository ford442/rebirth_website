import { normalizeBase } from './url';

/**
 * Canonical URLs for externally hosted archive assets.
 * Always use HTTPS — test.1ink.us redirects HTTP but some browsers
 * block or warn on mixed-content navigations from the GitHub Pages site.
 */
export const RBS_ARCHIVE_BASE_URL = 'https://test.1ink.us/rb338/archive/rbs-songs';

/** Relative path under public/ where same-origin demo files are stored. */
export const RBS_LOCAL_DEMO_PATH = 'archive/rbs-songs/demo';

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

/** Same-origin URL for a demo file under public/archive/rbs-songs/demo/. */
export function rbsLocalDemoUrl(base: string, filename: string): string {
  const root = normalizeBase(base);
  return `${root}/${RBS_LOCAL_DEMO_PATH}/${encodeURIComponent(filename)}`;
}

/**
 * Archive-relative path → same-origin public path for the playable core.
 * Keep in sync with scripts/sync-demo-songs.py / DEMO_LOCAL_BY_PATH.
 */
export const CORE_LOCAL_BY_PATH: Record<string, string> = {
  'By_Source/Rebirth_2.0/Complete/A Taste of Haste.rbs': `${RBS_LOCAL_DEMO_PATH}/a-taste-of-haste.rbs`,
  'Monthly_Archive/FEBRUARY/Orbiting your Heart.rbs': `${RBS_LOCAL_DEMO_PATH}/orbiting-your-heart.rbs`,
  'Monthly_Archive/SEPTEMBER/Troublemakers.rbs': `${RBS_LOCAL_DEMO_PATH}/troublemakers.rbs`,
  'Artists/Cavey/3_acid.rbs': `${RBS_LOCAL_DEMO_PATH}/cavey-3-acid.rbs`,
  'Artists/Noah_Cohn_Complete/20.rbs': `${RBS_LOCAL_DEMO_PATH}/noah-cohn-20.rbs`,
  'Artists/DJ Knightmare/amnesya.rbs': `${RBS_LOCAL_DEMO_PATH}/dj-knightmare-amnesya.rbs`,
  'By_Source/Rebirth_2.0/Complete/Metallicon City.rbs': `${RBS_LOCAL_DEMO_PATH}/metallicon-city.rbs`,
  'By_Source/Rebirth_2.0/Complete/#008.rbs': `${RBS_LOCAL_DEMO_PATH}/propellerhead-008.rbs`,
};

/** Same-origin URL for a public/ relative file. */
export function publicAssetUrl(base: string, relativePublicPath: string): string {
  const root = normalizeBase(base);
  const encoded = relativePublicPath
    .split('/')
    .map((segment) => encodeURIComponent(segment))
    .join('/');
  return `${root}/${encoded}`;
}

/**
 * Prefer a same-origin core copy (COEP-safe) when we ship the bytes;
 * otherwise the remote catalog URL.
 */
export function resolveArchivePlaybackUrl(base: string, relativePath: string): string {
  const local = CORE_LOCAL_BY_PATH[relativePath];
  if (local) return publicAssetUrl(base, local);
  return rbsDownloadUrl(relativePath);
}
