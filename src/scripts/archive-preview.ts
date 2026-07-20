/**
 * Archive folder page — wire Play buttons to the in-browser preview player.
 */
import { previewSong } from '../lib/player-events';
import { normalizeBase } from '../lib/url';

function getBase(): string {
  const meta = document.querySelector<HTMLMetaElement>('meta[name="base-url"]');
  return normalizeBase(meta?.content ?? '/rb338');
}

document.querySelectorAll<HTMLButtonElement>('.preview-btn').forEach((button) => {
  button.addEventListener('click', () => {
    const src = button.dataset.previewSrc;
    if (!src) return;
    const label = button.dataset.previewLabel ?? 'archive preview';
    const bpmRaw = button.dataset.previewBpm;
    const bpm = bpmRaw ? Number(bpmRaw) : undefined;
    previewSong(getBase(), { src, label, bpm });
  });
});
