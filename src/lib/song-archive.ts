export interface SongIndexEntry {
  filename: string;
  path: string;
  url: string;
  title: string;
  collection: string;
  subcollection: string | null;
  artist: string | null;
  bpm: number | null;
  fileSize: string;
  source: string;
  metaTitle?: string | null;
  metaAuthor?: string | null;
  metaBpm?: number | null;
  parseOk?: boolean;
}

/** Collect every ancestor folder path that contains indexed songs. */
export function collectFolderPaths(songs: SongIndexEntry[]): Set<string> {
  const paths = new Set<string>();
  songs.forEach((song) => {
    const segments = song.path.split('/');
    segments.pop();
    for (let i = 1; i <= segments.length; i++) {
      paths.add(segments.slice(0, i).join('/'));
    }
  });
  return paths;
}

/** Return immediate child folder paths for a given archive folder. */
export function childFolderPaths(folderPath: string, allFolderPaths: Set<string>): string[] {
  const prefix = folderPath ? `${folderPath}/` : '';
  const children = new Set<string>();

  allFolderPaths.forEach((path) => {
    if (!path.startsWith(prefix) || path === folderPath) return;
    const remainder = path.slice(prefix.length);
    const child = remainder.split('/')[0];
    if (child) children.add(`${prefix}${child}`);
  });

  return Array.from(children).sort((a, b) => a.localeCompare(b));
}
