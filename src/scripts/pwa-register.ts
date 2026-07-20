import { registerSW } from 'virtual:pwa-register';

let updateServiceWorker: ((reloadPage?: boolean) => Promise<void>) | undefined;

function getBannerElements() {
  return {
    banner: document.getElementById('pwa-update-banner'),
    reloadBtn: document.getElementById('pwa-update-reload'),
    dismissBtn: document.getElementById('pwa-update-dismiss'),
  };
}

function showUpdateBanner(): void {
  const { banner } = getBannerElements();
  if (!banner) return;
  banner.hidden = false;
}

function hideUpdateBanner(): void {
  const { banner } = getBannerElements();
  if (banner) banner.hidden = true;
}

function bindBannerControls(): void {
  const { reloadBtn, dismissBtn } = getBannerElements();
  reloadBtn?.addEventListener('click', () => {
    void updateServiceWorker?.(true);
    hideUpdateBanner();
  });
  dismissBtn?.addEventListener('click', () => {
    hideUpdateBanner();
  });
}

export function initPwaRegister(): void {
  bindBannerControls();

  updateServiceWorker = registerSW({
    immediate: true,
    onNeedRefresh() {
      showUpdateBanner();
    },
    onRegisteredSW(_swUrl, registration) {
      if (!registration) return;
      const check = () => registration.update().catch(() => undefined);
      window.addEventListener('focus', check);
      setInterval(check, 60 * 60 * 1000);
    },
  });
}

if (typeof window !== 'undefined') {
  initPwaRegister();
}
