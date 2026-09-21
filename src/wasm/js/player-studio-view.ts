/**
 * Studio view for the in-browser player: the per-device pattern grid, knob
 * panel, and the song metadata chips that summarise device state.
 *
 * This is the module DSP/device-parameter contributors touch — it owns the
 * mapping from `DeviceParam` (see `player-studio.ts`) to `bridge.setDeviceParam`.
 */

import type { DeviceId, ParsedSong, WasmStepData } from '../types/wasm-audio-song';
import {
  buildStepCells,
  defaultPatternCoords,
  DEVICE_INDEX,
  isAcidDevice,
  pickPattern,
  pickPatternExact,
  STUDIO_DEVICE_LABEL,
} from './player-studio';
import {
  applyStepToSong,
  createEditHistory,
  findDrumHit,
  nextStepValue,
  toWasmStep,
  type StepEdit,
} from './player-step-edit';
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
  /** Undo the most recent step edit. Returns false when the stack is empty. */
  undoLastEdit(): boolean;
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
  let stepEditingLive = false;
  const history = createEditHistory(64);

  function selectedNote(): number {
    const value = Number(dom.studioNote?.value);
    return Number.isFinite(value) ? value : 48;
  }

  function selectedDrumHit() {
    return findDrumHit(dom.studioDrum?.value ?? 'bd');
  }

  function setStudioLive(live: boolean) {
    stepEditingLive = live;
    dom.root.dataset.studioLive = live ? '1' : '0';
    dom.studioKnobs.forEach((el) => {
      el.disabled = !live;
    });
    dom.studioSteps.forEach((cell) => {
      cell.disabled = !live;
    });
    if (dom.studioNote) dom.studioNote.disabled = !live;
    if (dom.studioDrum) dom.studioDrum.disabled = !live;
    updateUndoButton();
    if (dom.studioHint) {
      dom.studioHint.textContent = live
        ? 'Click a step to edit it, shift-click to accent, UNDO to step back. Edits go into the ' +
          'engine working copy and play from the next loop; knob moves stay session-only. ' +
          'Nothing is saved to the .rbs file — reload it to get the original back.'
        : 'Live knobs and step editing need the WASM engine. Pattern grid still shows parsed steps when available.';
    }
  }

  function updateUndoButton() {
    if (!dom.studioUndo) return;
    dom.studioUndo.disabled = !stepEditingLive || history.size === 0;
  }

  /** Current value of a step, preferring the engine's working copy. */
  function readStep(stepIndex: number): WasmStepData {
    const bridge = deps.getBridge();
    if ('getStep' in bridge && typeof bridge.getStep === 'function') {
      const fromEngine = bridge.getStep(
        DEVICE_INDEX[selectedDevice],
        selectedBank,
        selectedPatternIndex,
        stepIndex
      );
      if (fromEngine) return toWasmStep(fromEngine);
    }
    const song = deps.getLoadedSong();
    const pattern = song
      ? song.patterns.find(
          (p) =>
            p.deviceId === selectedDevice &&
            p.bank === selectedBank &&
            p.patternIndex === selectedPatternIndex
        )
      : null;
    return toWasmStep(pattern?.steps[stepIndex]);
  }

  /** Send one patch to the engine and mirror it into the UI song summary. */
  function commitStep(
    device: DeviceId,
    bank: number,
    patternIndex: number,
    stepIndex: number,
    next: WasmStepData
  ): boolean {
    const bridge = deps.getBridge();
    const ok =
      'setStep' in bridge &&
      typeof bridge.setStep === 'function' &&
      bridge.setStep(DEVICE_INDEX[device], bank, patternIndex, stepIndex, next);
    if (!ok) return false;
    const song = deps.getLoadedSong();
    if (song) applyStepToSong(song, device, bank, patternIndex, stepIndex, next);
    return true;
  }

  function editStep(stepIndex: number, accentOnly: boolean) {
    if (!stepEditingLive || deps.isDegradedMode()) return;
    const previous = readStep(stepIndex);
    const next = nextStepValue(previous, selectedDevice, {
      accentOnly,
      note: selectedNote(),
      drumHit: selectedDrumHit(),
    });
    const edit: StepEdit = {
      deviceId: selectedDevice,
      bank: selectedBank,
      patternIndex: selectedPatternIndex,
      stepIndex,
      previous,
    };
    if (!commitStep(selectedDevice, selectedBank, selectedPatternIndex, stepIndex, next)) return;
    // Inverse patches only — a ParsedSong clone per keystroke would blow the
    // JS heap alongside the fixed 64 MiB WASM one.
    history.push(edit);
    updateUndoButton();
    renderStudio();
  }

  function undoLastEdit(): boolean {
    const edit = history.pop();
    updateUndoButton();
    if (!edit) return false;
    const restored = commitStep(
      edit.deviceId,
      edit.bank,
      edit.patternIndex,
      edit.stepIndex,
      edit.previous
    );
    if (!restored) return false;
    // Show the slot the undo actually touched, even after the user has
    // navigated somewhere else.
    selectedDevice = edit.deviceId;
    selectedBank = edit.bank;
    selectedPatternIndex = edit.patternIndex;
    if (dom.studioBank) dom.studioBank.value = String(selectedBank);
    if (dom.studioPattern) dom.studioPattern.value = String(selectedPatternIndex);
    renderStudio();
    return true;
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
    // While the grid is editable it must show exactly the slot a click would
    // write to; the forgiving fallback is only right for a read-only preview.
    const lookup = stepEditingLive ? pickPatternExact : pickPattern;
    const pattern = loadedSong
      ? lookup(loadedSong, selectedDevice, selectedBank, selectedPatternIndex)
      : null;
    const cells = buildStepCells(pattern, selectedDevice);
    dom.studioGrid.querySelectorAll<HTMLElement>('[data-studio-step]').forEach((cell) => {
      const index = Number(cell.dataset.studioStep);
      const model = cells[index];
      if (!model) return;
      cell.classList.toggle('is-active', model.active);
      cell.setAttribute('aria-pressed', model.active ? 'true' : 'false');
      cell.classList.toggle('is-accent', model.accent);
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
    // The edit controls are device-family specific: a note for the 303s, an
    // instrument to toggle for the drum machines.
    if (dom.studioNoteWrap) dom.studioNoteWrap.hidden = !acid;
    if (dom.studioDrumWrap) dom.studioDrumWrap.hidden = acid;
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
    // A new song means the previous song's inverse patches no longer describe
    // anything that exists.
    history.clear();
    updateUndoButton();
    const coords = defaultPatternCoords(song, selectedDevice);
    selectedBank = coords.bank;
    selectedPatternIndex = coords.patternIndex;
    if (studioBank) studioBank.value = String(selectedBank);
    if (studioPattern) studioPattern.value = String(selectedPatternIndex);
    renderStudio();
  }

  function attachEvents() {
    dom.studioSteps.forEach((cell) => {
      cell.addEventListener('click', (event) => {
        const index = Number(cell.dataset.studioStep);
        if (!Number.isFinite(index)) return;
        editStep(index, (event as MouseEvent).shiftKey);
      });
    });

    dom.studioUndo?.addEventListener('click', () => {
      undoLastEdit();
    });

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
    undoLastEdit,
    renderMetadataPanel,
    applyPatternPreview,
    renderStudio,
    attachEvents,
  };
}
