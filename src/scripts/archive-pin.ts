/**
 * Pin the playable core to Cache Storage + IndexedDB after SHA-256 verify.
 */
import { normalizeBase } from '../lib/url';
import { publicAssetUrl } from '../lib/archive-urls';

const IDB_NAME = 'rb338-archive';
const IDB_STORE = 'pinned';
const CACHE_NAME = 'rb338-pinned';

interface CoreSong {
  path: string;
  sha256: string;
  bytes: number;
  localPath: string;
}

interface CoreManifest {
  songs: CoreSong[];
}

function getBase(): string {
  const meta = document.querySelector<HTMLMetaElement>('meta[name="base-url"]');
  return normalizeBase(meta?.content ?? '/rebirth_website');
}

function coreUrl(): string {
  const prefix = getBase() ? `${getBase()}/` : '/';
  return `${prefix}data/archive-core.json`;
}

async function sha256Hex(buffer: ArrayBuffer): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', buffer);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');
}

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(IDB_NAME, 1);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(IDB_STORE)) {
        db.createObjectStore(IDB_STORE, { keyPath: 'path' });
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function putRecord(record: {
  path: string;
  sha256: string;
  bytes: number;
  pinnedAt: string;
}): Promise<void> {
  const db = await openDb();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(IDB_STORE, 'readwrite');
    tx.objectStore(IDB_STORE).put(record);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
  db.close();
}

export function mountArchivePin(): void {
  const root = document.querySelector<HTMLElement>('[data-archive-pin]');
  if (!root) return;
  const button = root.querySelector<HTMLButtonElement>('[data-archive-pin-btn]');
  const status = root.querySelector<HTMLElement>('[data-archive-pin-status]');
  if (!button || !status) return;

  const setStatus = (text: string) => {
    status.textContent = text;
  };

  button.addEventListener('click', async () => {
    button.disabled = true;
    setStatus('Loading integrity manifest…');
    try {
      const res = await fetch(coreUrl());
      if (!res.ok) throw new Error(`manifest ${res.status}`);
      const manifest = (await res.json()) as CoreManifest;
      const songs = manifest.songs ?? [];
      if (songs.length === 0) throw new Error('empty core manifest');

      const cache = await caches.open(CACHE_NAME);
      const base = getBase();
      let pinned = 0;
      for (const song of songs) {
        setStatus(`Verifying ${song.localPath}…`);
        const url = publicAssetUrl(base, song.localPath);
        const fileRes = await fetch(url);
        if (!fileRes.ok) throw new Error(`${song.localPath} ${fileRes.status}`);
        const buffer = await fileRes.arrayBuffer();
        const digest = await sha256Hex(buffer);
        if (digest !== song.sha256) {
          throw new Error(`hash mismatch ${song.localPath}`);
        }
        await cache.put(
          url,
          new Response(buffer, { headers: { 'Content-Type': 'application/octet-stream' } })
        );
        await putRecord({
          path: song.path,
          sha256: digest,
          bytes: buffer.byteLength,
          pinnedAt: new Date().toISOString(),
        });
        pinned += 1;
      }
      setStatus(`Pinned ${pinned} core songs. Verified SHA-256. Available offline.`);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'pin failed';
      setStatus(`Pin failed: ${message}`);
    } finally {
      button.disabled = false;
    }
  });
}
