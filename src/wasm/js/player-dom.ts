/**
 * DOM element lookup for the in-browser player UI.
 *
 * A single place to `querySelector` every element `player-ui.ts` (and its
 * transport/studio view collaborators) touch, so those modules only ever
 * deal with already-typed, already-nullable-checked refs.
 */

export interface PlayerDom {
  /** The player root element passed to `initPlayerUI` — for selectors not covered below. */
  root: HTMLElement;
  dropZone: HTMLElement | null;
  fileInput: HTMLInputElement | null;
  btnPlay: HTMLButtonElement | null;
  btnStop: HTMLButtonElement | null;
  btnLoad: HTMLButtonElement | null;
  metaEl: HTMLElement | null;
  metaTitle: HTMLElement | null;
  metaAuthor: HTMLElement | null;
  metaBpm: HTMLElement | null;
  metaDevices: HTMLElement | null;
  messageEl: HTMLElement | null;
  demoSelect: HTMLSelectElement | null;
  btnDemoLoad: HTMLButtonElement | null;
  volumeSlider: HTMLInputElement | null;
  volumeValue: HTMLElement | null;
  tempoSlider: HTMLInputElement | null;
  tempoValue: HTMLElement | null;
  fallbackEl: HTMLElement | null;
  fallbackHeadline: HTMLElement | null;
  fallbackDetail: HTMLElement | null;
  fallbackMode: HTMLElement | null;
  toastStack: HTMLElement | null;
  lcdStatus: HTMLElement | null;
  lcdStatusPanel: HTMLElement | null;
  lcdBpm: HTMLElement | null;
  lcdBar: HTMLElement | null;
  ledLabel: HTMLElement | null;
  stepDots: NodeListOf<Element>;
  meterSegments: NodeListOf<Element>;
  studioGrid: HTMLElement | null;
  studioHint: HTMLElement | null;
  studioBank: HTMLSelectElement | null;
  studioPattern: HTMLSelectElement | null;
  studioTabs: NodeListOf<HTMLButtonElement>;
  studioKnobs: NodeListOf<HTMLInputElement | HTMLSelectElement>;
}

export function queryPlayerDom(playerEl: HTMLElement): PlayerDom {
  return {
    root: playerEl,
    dropZone: playerEl.querySelector('#rbsDropZone'),
    fileInput: playerEl.querySelector('#rbsFileInput'),
    btnPlay: playerEl.querySelector('#rbsBtnPlay'),
    btnStop: playerEl.querySelector('#rbsBtnStop'),
    btnLoad: playerEl.querySelector('#rbsBtnLoad'),
    metaEl: playerEl.querySelector('#rbsMeta'),
    metaTitle: playerEl.querySelector('#rbsMetaTitle'),
    metaAuthor: playerEl.querySelector('#rbsMetaAuthor'),
    metaBpm: playerEl.querySelector('#rbsMetaBpm'),
    metaDevices: playerEl.querySelector('#rbsMetaDevices'),
    messageEl: playerEl.querySelector('#rbsMessage'),
    demoSelect: playerEl.querySelector('#rbsDemoSelect'),
    btnDemoLoad: playerEl.querySelector('#rbsBtnDemoLoad'),
    volumeSlider: playerEl.querySelector('#rbsVolume'),
    volumeValue: playerEl.querySelector('#rbsVolumeValue'),
    tempoSlider: playerEl.querySelector('#rbsTempo'),
    tempoValue: playerEl.querySelector('#rbsTempoValue'),
    fallbackEl: playerEl.querySelector('#rbsFallback'),
    fallbackHeadline: playerEl.querySelector('#rbsFallbackHeadline'),
    fallbackDetail: playerEl.querySelector('#rbsFallbackDetail'),
    fallbackMode: playerEl.querySelector('#rbsFallbackMode'),
    toastStack: playerEl.querySelector('#rbsToastStack'),
    lcdStatus: playerEl.querySelector('.player-lcd-status .lcd-value'),
    lcdStatusPanel: playerEl.querySelector('.player-lcd-status'),
    lcdBpm: playerEl.querySelector('.player-lcd-bpm .lcd-value'),
    lcdBar: playerEl.querySelector('.player-lcd-bar .lcd-value'),
    ledLabel: playerEl.querySelector('.player-led'),
    stepDots: playerEl.querySelectorAll('.step-dot'),
    meterSegments: playerEl.querySelectorAll('.meter-segment'),
    studioGrid: playerEl.querySelector('#rbsStudioGrid'),
    studioHint: playerEl.querySelector('#rbsStudioHint'),
    studioBank: playerEl.querySelector('#rbsStudioBank'),
    studioPattern: playerEl.querySelector('#rbsStudioPattern'),
    studioTabs: playerEl.querySelectorAll<HTMLButtonElement>('[data-studio-device]'),
    studioKnobs: playerEl.querySelectorAll<HTMLInputElement | HTMLSelectElement>(
      '[data-studio-param]'
    ),
  };
}
