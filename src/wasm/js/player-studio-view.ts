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
  DeviceParam,
  isAcidDevice,
  pickPattern,
  pickPatternExact,
  STUDIO_DEVICE_LABEL,
  STUDIO_DEVICES,
} from './player-studio';
import {
  applyStepToSong,
  createEditHistory,
  findDrumHit,
  nextStepValue,
  toggleDrumHit,
  toWasmStep,
  type StepEdit,
} from './player-step-edit';
import type { MidiAction } from './player-midi';
import {
  emptyPatch,
  type PatchParam,
  type PatchStep,
  type StudioPatch,
} from '../../lib/studio-patch';
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
  /**
   * Apply one decoded WebMIDI action (see `player-midi.ts`).
   *
   * Returns false when nothing was written — the studio is not live, the
   * action was a note-off, or the engine rejected the patch.
   */
  applyMidiAction(action: MidiAction): boolean;
  /** Tell the grid where the sequencer is, so MIDI pads land on that step. */
  setPlaybackStep(step: number): void;
  /** Everything edited this session, as a shareable `?p=` patch. */
  capturePatch(): StudioPatch;
  /** Replay a decoded `?p=` patch into the engine. Returns how much stuck. */
  applyPatch(patch: StudioPatch): number;
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

  /**
   * Where the next MIDI note is written.
   *
   * While the sequencer runs it tracks the playhead, so pads punch into the
   * step you hear. Stopped, it is a step-record cursor that advances after
   * each 303 note — the usual way to tap a line in without a mouse.
   */
  let playbackStep = 0;
  let recordCursor = 0;

  /**
   * Session edits, keyed by slot so re-editing one step replaces its entry
   * rather than growing the share link. This is the *current* state of every
   * touched slot — not the undo stack, which is a list of inverse patches.
   */
  const editedSteps = new Map<string, PatchStep>();
  const movedParams = new Map<string, PatchParam>();

  function recordStepEdit(
    device: DeviceId,
    bank: number,
    patternIndex: number,
    stepIndex: number,
    step: WasmStepData
  ) {
    const deviceIndex = DEVICE_INDEX[device];
    editedSteps.set(`${deviceIndex}:${bank}:${patternIndex}:${stepIndex}`, {
      deviceIndex,
      bank,
      patternIndex,
      stepIndex,
      note: step.note ?? 0,
      drumExtra: step.drumExtra ?? 0,
      active: Boolean(step.active),
      accent: Boolean(step.accent),
      slide: Boolean(step.slide),
    });
  }

  function recordParamMove(deviceIndex: number, paramId: number, value: number) {
    movedParams.set(`${deviceIndex}:${paramId}`, { deviceIndex, paramId, value });
  }

  function capturePatch(): StudioPatch {
    const patch = emptyPatch();
    patch.steps = Array.from(editedSteps.values());
    patch.params = Array.from(movedParams.values());
    return patch;
  }

  /**
   * Replay a shared patch. Steps go through the same `setStep` path as a
   * click, so a link can only reach state the UI could have produced.
   */
  function applyPatch(patch: StudioPatch): number {
    const bridge = deps.getBridge();
    let applied = 0;
    for (const step of patch.steps) {
      const device = STUDIO_DEVICES[step.deviceIndex]?.id;
      if (!device) continue;
      const next: WasmStepData = {
        active: step.active,
        note: step.note,
        drumExtra: step.drumExtra,
        accent: step.accent,
        slide: step.slide,
      };
      if (commitStep(device, step.bank, step.patternIndex, step.stepIndex, next)) {
        recordStepEdit(device, step.bank, step.patternIndex, step.stepIndex, next);
        applied++;
      }
    }
    for (const param of patch.params) {
      const ok =
        'setDeviceParam' in bridge &&
        typeof bridge.setDeviceParam === 'function' &&
        bridge.setDeviceParam(param.deviceIndex, param.paramId, param.value);
      if (ok) {
        recordParamMove(param.deviceIndex, param.paramId, param.value);
        applied++;
      }
    }
    if (applied > 0) renderStudio();
    return applied;
  }

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
    recordStepEdit(device, bank, patternIndex, stepIndex, next);
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

  /** Switch the visible device, following the song's default slot for it. */
  function selectDevice(id: DeviceId) {
    selectedDevice = id;
    const loadedSong = deps.getLoadedSong();
    if (loadedSong) {
      const coords = defaultPatternCoords(loadedSong, selectedDevice);
      selectedBank = coords.bank;
      selectedPatternIndex = coords.patternIndex;
      if (dom.studioBank) dom.studioBank.value = String(selectedBank);
      if (dom.studioPattern) dom.studioPattern.value = String(selectedPatternIndex);
    }
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

  function setPlaybackStep(step: number) {
    if (Number.isFinite(step)) playbackStep = ((step % 16) + 16) % 16;
  }

  /** The step a MIDI event writes to. */
  function midiTargetStep(): number {
    return deps.getBridge().playerStatus === 'playing' ? playbackStep : recordCursor;
  }

  /**
   * Write one step from MIDI, reusing the click path's undo history so a
   * mis-hit pad is as recoverable as a mis-click.
   */
  function commitMidiStep(
    device: DeviceId,
    stepIndex: number,
    next: WasmStepData,
    previous: WasmStepData
  ): boolean {
    const edit: StepEdit = {
      deviceId: device,
      bank: selectedBank,
      patternIndex: selectedPatternIndex,
      stepIndex,
      previous,
    };
    if (!commitStep(device, selectedBank, selectedPatternIndex, stepIndex, next)) return false;
    history.push(edit);
    updateUndoButton();
    renderStudio();
    return true;
  }

  function applyMidiAction(action: MidiAction): boolean {
    if (!stepEditingLive || deps.isDegradedMode()) return false;

    if (action.kind === 'param') {
      const bridge = deps.getBridge();
      const ok =
        'setDeviceParam' in bridge &&
        typeof bridge.setDeviceParam === 'function' &&
        bridge.setDeviceParam(DEVICE_INDEX[selectedDevice], action.paramId, action.value);
      if (!ok) return false;
      // Mirror the controller move onto the on-screen knob, so the panel
      // never disagrees with what the engine is doing.
      recordParamMove(DEVICE_INDEX[selectedDevice], action.paramId, action.value);
      const knob = action.paramId === DeviceParam.Cutoff ? 'cutoff' : 'resonance';
      const el = dom.root.querySelector<HTMLInputElement>(`[data-studio-knob="${knob}"]`);
      if (el) el.value = String(action.value);
      const device = deps.getLoadedSong()?.devices.find((d) => d.deviceId === selectedDevice);
      if (device) device.knobs[knob] = action.value;
      return true;
    }

    // Note-offs carry no edit: a step is a latched state, not a gate.
    if (!action.on) return false;

    const stepIndex = midiTargetStep();

    if (action.kind === 'acid-note') {
      // The incoming channel picks the 303, so a two-channel controller can
      // play both lines without touching the device tabs.
      if (action.device !== selectedDevice) {
        selectDevice(action.device);
      }
      const previous = readStep(stepIndex);
      const next: WasmStepData = {
        active: true,
        note: action.note,
        drumExtra: 0,
        accent: action.accent,
        slide: false,
      };
      const ok = commitMidiStep(action.device, stepIndex, next, previous);
      if (ok && deps.getBridge().playerStatus !== 'playing') {
        recordCursor = (stepIndex + 1) % 16;
      }
      return ok;
    }

    const hit = findDrumHit(action.drumKey);
    if (!hit) return false;
    // Pads target whichever drum machine is selected; the 808 is the default
    // so a controller works before anyone touches the tabs.
    const drumDevice: DeviceId = isAcidDevice(selectedDevice) ? 'tr808' : selectedDevice;
    if (drumDevice !== selectedDevice) selectDevice(drumDevice);
    const previous = readStep(stepIndex);
    const next = toggleDrumHit(previous, hit);
    if (action.accent && next.active) next.accent = true;
    return commitMidiStep(drumDevice, stepIndex, next, previous);
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
        selectDevice(id);
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
        if (ok) recordParamMove(deviceIndex, paramId, value);
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
    applyMidiAction,
    setPlaybackStep,
    capturePatch,
    applyPatch,
    undoLastEdit,
    renderMetadataPanel,
    applyPatternPreview,
    renderStudio,
    attachEvents,
  };
}
