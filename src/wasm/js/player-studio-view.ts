/**
 * Studio view for the in-browser player: the per-device pattern grid, knob
 * panel, and the song metadata chips that summarise device state.
 *
 * This is the module DSP/device-parameter contributors touch — it owns the
 * mapping from `DeviceParam` (see `player-studio.ts`) to `bridge.setDeviceParam`.
 */

import type { DeviceId, ParsedSong } from '../types/wasm-audio';
import {
  buildStepCells,
  defaultPatternCoords,
  DEVICE_INDEX,
  isAcidDevice,
  pickPattern,
  STUDIO_DEVICE_LABEL,
} from './player-studio';
import type { PlayerDom } from './player-dom';
import type { PlayerBridge } from './player-transport';

export interface StudioViewDeps {
  dom: PlayerDom;
  getLoadedSong: () => ParsedSong | null;
  getBridge: () => PlayerBridge;
  isDegradedMode: () => boolean;
}

export interface StudioView {
  setStudioLive(live: boolean): void;
  renderMetadataPanel(song: ParsedSong): void;
  applyPatternPreview(song: ParsedSong): void;
  renderStudio(): void;
  /** Wires the device tabs, bank/pattern selects, and knob inputs. */
  attachEvents(): void;
}

export function createStudioView(deps: StudioViewDeps): StudioView {
  const { dom } = deps;

  let selectedDevice: DeviceId = 'tb303-a';
  let selectedBank = 0;
  let selectedPatternIndex = 0;

  function setStudioLive(live: boolean) {
    dom.root.dataset.studioLive = live ? '1' : '0';
    dom.studioKnobs.forEach((el) => {
      el.disabled = !live;
    });
    if (dom.studioHint) {
      dom.studioHint.textContent = live
        ? 'Session knobs — cutoff, reso, decay, and mixer affect playback. Not saved to the file.'
        : 'Live knobs need the WASM engine. Pattern grid still shows parsed steps when available.';
    }
  }

  function applyPatternPreview(song: ParsedSong) {
    const patternSteps = new Set<number>();
    for (const pattern of song.patterns) {
      pattern.steps.forEach((stepData, index) => {
        if (stepData.active) patternSteps.add(index);
      });
    }
    const bridge = deps.getBridge();
    dom.stepDots.forEach((dot, i) => {
      dot.classList.toggle('is-pattern', patternSteps.has(i));
      if (bridge.playerStatus !== 'playing') {
        dot.classList.toggle('is-active', patternSteps.has(i));
      }
    });
  }

  function renderStudio() {
    if (!dom.studioGrid) return;
    const loadedSong = deps.getLoadedSong();
    const pattern = loadedSong
      ? pickPattern(loadedSong, selectedDevice, selectedBank, selectedPatternIndex)
      : null;
    const cells = buildStepCells(pattern, selectedDevice);
    dom.studioGrid.querySelectorAll<HTMLElement>('[data-studio-step]').forEach((cell) => {
      const index = Number(cell.dataset.studioStep);
      const model = cells[index];
      if (!model) return;
      cell.classList.toggle('is-active', model.active);
      const label = cell.querySelector('[data-studio-label]');
      const flags = cell.querySelector('[data-studio-flags]');
      if (label) label.textContent = model.label;
      if (flags) {
        const marks: string[] = [];
        if (model.accent) marks.push('A');
        if (model.slide) marks.push('S');
        flags.textContent = marks.join(' ');
      }
    });

    dom.studioTabs.forEach((tab) => {
      const on = tab.dataset.studioDevice === selectedDevice;
      tab.classList.toggle('is-selected', on);
      tab.setAttribute('aria-selected', on ? 'true' : 'false');
    });

    const device = loadedSong?.devices.find((d) => d.deviceId === selectedDevice);
    const acid = isAcidDevice(selectedDevice);
    dom.root.querySelectorAll<HTMLElement>('[data-knob-wrap]').forEach((wrap) => {
      const key = wrap.dataset.knobWrap;
      const acidOnly =
        key === 'cutoff' || key === 'resonance' || key === 'envMod' || key === 'waveform';
      wrap.hidden = Boolean(acidOnly && !acid);
    });

    const applyKnob = (name: string, value: number | boolean) => {
      const el = dom.root.querySelector<HTMLInputElement | HTMLSelectElement>(
        `[data-studio-knob="${name}"]`
      );
      if (!el) return;
      if (el instanceof HTMLInputElement && el.type === 'checkbox') {
        el.checked = Boolean(value);
      } else {
        el.value = String(value);
      }
    };
    if (device) {
      applyKnob('tune', device.knobs.tune ?? 0.5);
      applyKnob('cutoff', device.knobs.cutoff ?? 0.5);
      applyKnob('resonance', device.knobs.resonance ?? 0.5);
      applyKnob('envMod', device.knobs.envMod ?? 0.5);
      applyKnob('decay', device.knobs.decay ?? 0.5);
      applyKnob('accent', device.knobs.accent ?? 0.5);
      applyKnob('level', device.level ?? 0.8);
      applyKnob('pan', device.pan ?? 0.5);
      applyKnob('waveform', device.waveform ?? 0);
      applyKnob('mute', device.muted);
    }
  }

  function renderMetadataPanel(song: ParsedSong) {
    const { metaTitle, metaAuthor, metaBpm, metaEl, metaDevices, studioBank, studioPattern } = dom;
    if (metaTitle) metaTitle.textContent = song.title || 'Untitled';
    if (metaAuthor) metaAuthor.textContent = song.author || 'Unknown';
    if (metaBpm) metaBpm.textContent = `${Math.round(song.bpm)} BPM`;
    metaEl?.classList.remove('is-hidden');

    if (metaDevices) {
      metaDevices.innerHTML = '';
      const activeDevices =
        song.devices.length > 0
          ? song.devices
          : [
              {
                deviceId: 'tb303-a' as const,
                knobs: {},
                muted: false,
                level: 0.8,
                pan: 0.5,
                waveform: 0,
                initialPatternBank: 0,
                initialPatternIndex: 0,
              },
            ];

      for (const device of activeDevices) {
        const chip = document.createElement('span');
        chip.className = `meta-device${device.muted ? ' meta-device--muted' : ''}`;
        const label = STUDIO_DEVICE_LABEL[device.deviceId] ?? device.deviceId;
        const patternCount = song.patterns.filter((p) => p.deviceId === device.deviceId).length;
        chip.innerHTML = `<span class="meta-device__led" aria-hidden="true"></span><span class="meta-device__label">${label}</span><span class="meta-device__count">${patternCount}P</span>`;
        chip.title = `${label}${device.muted ? ' (muted)' : ''} — ${patternCount} patterns`;
        metaDevices.appendChild(chip);
      }
    }

    applyPatternPreview(song);
    const coords = defaultPatternCoords(song, selectedDevice);
    selectedBank = coords.bank;
    selectedPatternIndex = coords.patternIndex;
    if (studioBank) studioBank.value = String(selectedBank);
    if (studioPattern) studioPattern.value = String(selectedPatternIndex);
    renderStudio();
  }

  function attachEvents() {
    dom.studioTabs.forEach((tab) => {
      tab.addEventListener('click', () => {
        const id = tab.dataset.studioDevice as DeviceId | undefined;
        if (!id) return;
        selectedDevice = id;
        const loadedSong = deps.getLoadedSong();
        if (loadedSong) {
          const coords = defaultPatternCoords(loadedSong, selectedDevice);
          selectedBank = coords.bank;
          selectedPatternIndex = coords.patternIndex;
          if (dom.studioBank) dom.studioBank.value = String(selectedBank);
          if (dom.studioPattern) dom.studioPattern.value = String(selectedPatternIndex);
        }
        renderStudio();
      });
    });

    dom.studioBank?.addEventListener('change', () => {
      selectedBank = Number(dom.studioBank!.value) || 0;
      renderStudio();
    });
    dom.studioPattern?.addEventListener('change', () => {
      selectedPatternIndex = Number(dom.studioPattern!.value) || 0;
      renderStudio();
    });

    dom.studioKnobs.forEach((el) => {
      const apply = () => {
        if (deps.isDegradedMode()) return;
        const paramId = Number(el.dataset.studioParam);
        if (!Number.isFinite(paramId)) return;
        let value = 0;
        if (el instanceof HTMLInputElement && el.type === 'checkbox') {
          value = el.checked ? 1 : 0;
        } else if (el instanceof HTMLSelectElement) {
          value = Number(el.value);
        } else if (el instanceof HTMLInputElement) {
          value = Number(el.value);
        }
        const deviceIndex = DEVICE_INDEX[selectedDevice];
        const bridge = deps.getBridge();
        const ok =
          'setDeviceParam' in bridge &&
          typeof bridge.setDeviceParam === 'function' &&
          bridge.setDeviceParam(deviceIndex, paramId, value);
        const loadedSong = deps.getLoadedSong();
        if (ok && loadedSong) {
          const device = loadedSong.devices.find((d) => d.deviceId === selectedDevice);
          const knob = el.dataset.studioKnob;
          if (device && knob) {
            if (knob === 'mute') device.muted = value >= 0.5;
            else if (knob === 'level') device.level = value;
            else if (knob === 'pan') device.pan = value;
            else if (knob === 'waveform') device.waveform = value;
            else device.knobs[knob] = value;
          }
        }
      };
      el.addEventListener('input', apply);
      el.addEventListener('change', apply);
    });
  }

  return {
    setStudioLive,
    renderMetadataPanel,
    applyPatternPreview,
    renderStudio,
    attachEvents,
  };
}
