/**
 * Blob download helper.
 *
 * Kept separate from the player UI because the constraint it encodes is
 * environmental, not cosmetic: a blob: URL download only works on a real
 * origin. In a sandboxed preview frame the anchor click is silently
 * swallowed, so callers get a clear failure instead of a button that appears
 * to do nothing.
 *
 * Nothing here touches import.meta.env.BASE_URL — a blob: URL is not a site
 * path, so the base path does not apply to it. Only fetches of archive assets
 * need the base, and those happen elsewhere.
 */

export interface DownloadResult {
  ok: boolean;
  reason?: string;
}

/** True when this document can actually start a download. */
export function canDownload(): boolean {
  if (typeof document === 'undefined' || typeof URL === 'undefined') return false;
  if (typeof URL.createObjectURL !== 'function') return false;
  // A sandboxed iframe without allow-downloads reports an opaque origin.
  if (typeof window !== 'undefined' && window.origin === 'null') return false;
  return true;
}

/** Trigger a browser download for `bytes`. */
export function downloadBytes(
  filename: string,
  bytes: Uint8Array,
  mimeType = 'application/octet-stream'
): DownloadResult {
  if (!canDownload()) {
    return {
      ok: false,
      reason: 'Downloads are blocked in this preview frame — open the page directly.',
    };
  }

  let url = '';
  try {
    // Copy into a fresh ArrayBuffer: `bytes` may be a view over the WASM heap
    // (or a subarray), and Blob would otherwise capture the whole buffer.
    const copy = new Uint8Array(bytes.byteLength);
    copy.set(bytes);
    const blob = new Blob([copy], { type: mimeType });
    url = URL.createObjectURL(blob);

    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = filename;
    anchor.rel = 'noopener';
    anchor.style.display = 'none';
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();

    return { ok: true };
  } catch (err) {
    return { ok: false, reason: err instanceof Error ? err.message : 'Download failed.' };
  } finally {
    // Revoke on the next tick; revoking synchronously can cancel the download
    // in some browsers before it has read the blob.
    if (url) {
      setTimeout(() => URL.revokeObjectURL(url), 10_000);
    }
  }
}
