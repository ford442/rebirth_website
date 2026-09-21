/**
 * WasmAudioBridge — typed JavaScript wrapper around the Emscripten-generated
 * WASM audio engine.
 *
 * Responsibilities:
 *   1. Load the Emscripten JS glue + .wasm binary
 *   2. Initialise the AudioContext and AudioWorklet
 *   3. Expose a clean TypeScript API for the Astro UI components
 *   4. Marshal ArrayBuffers into WASM memory for .rbs parsing
 *   5. Forward playback position updates from the audio thread to the UI
 *
 * Usage:
 *   const bridge = new WasmAudioBridge();
 *   await bridge.init();
 *   const song = await bridge.loadRbsFile(arrayBuffer);
 *   bridge.play();
 */

import type {
  EngineConfig,
  EngineError,
  EngineModule,
  PlaybackPosition,
  RbsAudioEngineInstance,
  RenderedFile,
} from '../types/wasm-audio-engine';
import type { ParsedMod } from '../types/wasm-audio-mod';
import type { ParsedSong, PlayerStatus, WasmStepData } from '../types/wasm-audio-song';

import { MASTER_BUS } from '../types/wasm-audio-engine';
import { MOD_LOAD_STATUSES } from '../types/wasm-audio-mod';

import { wasmAudioConfig } from '../audio-module.config';
import type { AudioContextDiagnostics } from '../types/wasm-audio-config';
import { toUiParsedMod, toUiParsedSong } from '../types/wasm-audio-mapping';
import { WasmInitError, INIT_FAILURE_MESSAGES } from './rbs-init-errors';
import {
  attachAudioContextLifecycle,
  createProductionAudioContext,
  type AudioContextLifecycle,
} from './create-audio-context';
import { waitForCrossOriginIsolation } from '../../scripts/coi-bootstrap';
import { locateWasmAsset } from './wasm-locate-file';
import { mallocCopy } from './wasm-engine-io';
import { BounceClient, type BouncePayload } from './wasm-bounce';

/** Callback invoked when playback position changes (bar, step). */
export type PositionCallback = (bar: number, step: number) => void;

/** Callback invoked when player status changes. */
export type StatusCallback = (status: PlayerStatus) => void;

export class WasmAudioBridge {
  private module: EngineModule | null = null;
  private audioContext: AudioContext | null = null;
  private workletNode: AudioWorkletNode | null = null;
  private gainNode: GainNode | null = null;
  /** Exposed publicly so integration tests can inspect engine state. */
  public enginePtr: RbsAudioEngineInstance | null = null;
  private status: PlayerStatus = 'idle';
  private onPosition: PositionCallback | null = null;
  private onStatus: StatusCallback | null = null;
  private volume = 0.8;
  private positionPollId: number | null = null;
  private _audioDiagnostics: AudioContextDiagnostics | null = null;
  private _audioLifecycle: AudioContextLifecycle | null = null;
  private _bounce = new BounceClient();

  // Kept so an offline bounce can rebuild exactly what is playing: the source
  // files plus any live knob moves, which the engine treats as session-only.
  private _songBuffer: ArrayBuffer | null = null;
  private _modBuffer: ArrayBuffer | null = null;
  private _songTitle = '';
  private _paramOverrides = new Map<string, { deviceId: number; paramId: number; value: number }>();

  /** The instantiated Emscripten module (heap probe / integration tests). */
  get wasmModule(): EngineModule | null {
    return this.module;
  }

  /** Is the bridge initialised and ready to load files? */
  get isReady(): boolean {
    return this.status === 'ready' || this.status === 'playing';
  }

  /** Current player status. */
  get playerStatus(): PlayerStatus {
    return this.status;
  }

  /** Current output volume (0.0–1.0). */
  get outputVolume(): number {
    return this.volume;
  }

  /** The underlying AudioContext (exposed for integration tests). */
  get ctx(): AudioContext | null {
    return this.audioContext;
  }

  /** Audio device metrics collected during init (null before init completes). */
  get audioDiagnostics(): AudioContextDiagnostics | null {
    return this._audioDiagnostics;
  }

  /** The master gain node (exposed for integration tests). */
  get masterGain(): GainNode | null {
    return this.gainNode;
  }

  /** Register a callback for playback position updates. */
  setPositionCallback(cb: PositionCallback | null) {
    this.onPosition = cb;
  }

  /** Register a callback for status changes. */
  setStatusCallback(cb: StatusCallback | null) {
    this.onStatus = cb;
  }

  /**
   * Initialise the bridge:
   *   1. Load Emscripten glue script
   *   2. Instantiate WASM module
   *   3. Create AudioContext (may start suspended until play() resumes it)
   *   4. Register the Emscripten "rbs-player" worklet via C++ — do not call
   *      audioWorklet.addModule() with a second processor of the same name
   *   5. Create RbsAudioEngine instance in WASM
   */
  async init(): Promise<void> {
    this._setStatus('loading');

    try {
      // 1. Check browser support
      if (typeof WebAssembly !== 'object') {
        throw new WasmInitError({
          reason: 'unsupported-browser',
          message: INIT_FAILURE_MESSAGES['unsupported-browser'],
        });
      }
      if (typeof AudioWorkletNode === 'undefined') {
        throw new WasmInitError({
          reason: 'worklet-unavailable',
          message: INIT_FAILURE_MESSAGES['worklet-unavailable'],
        });
      }

      if (typeof crossOriginIsolated !== 'undefined' && !crossOriginIsolated) {
        const isolated = await waitForCrossOriginIsolation();
        if (!isolated) {
          throw new WasmInitError({
            reason: 'not-cross-origin-isolated',
            message: INIT_FAILURE_MESSAGES['not-cross-origin-isolated'],
          });
        }
      }

      // 2. Probe WASM asset availability before importing glue
      let wasmProbe: Response;
      try {
        wasmProbe = await fetch(wasmAudioConfig.wasmPath, { method: 'HEAD' });
      } catch {
        throw new WasmInitError({
          reason: 'wasm-unavailable',
          message: INIT_FAILURE_MESSAGES['wasm-unavailable'],
        });
      }
      if (!wasmProbe.ok) {
        throw new WasmInitError({
          reason: 'wasm-unavailable',
          message: INIT_FAILURE_MESSAGES['wasm-unavailable'],
        });
      }

      // 3. Load Emscripten glue (dynamic import of the generated JS)
      let glueModule: { default: unknown };
      try {
        glueModule = await import(/* @vite-ignore */ wasmAudioConfig.glueScriptPath);
      } catch (importErr) {
        throw new WasmInitError({
          reason: 'wasm-load-failed',
          message: INIT_FAILURE_MESSAGES['wasm-load-failed'],
          cause: importErr,
        });
      }
      const moduleFactory = glueModule.default as (opts: {
        locateFile: (path: string) => string;
      }) => Promise<EngineModule>;

      this.module = await moduleFactory({
        locateFile: (path: string) =>
          locateWasmAsset(path, {
            wasmPath: wasmAudioConfig.wasmPath,
            glueScriptPath: wasmAudioConfig.glueScriptPath,
            workletPath: wasmAudioConfig.workletPath,
          }),
      });

      // 3. Create AudioContext (retry without sampleRate on NotSupportedError)
      const { context, diagnostics } = createProductionAudioContext(
        wasmAudioConfig.preferredSampleRate,
        wasmAudioConfig.latencyHint
      );
      this.audioContext = context;
      this._audioDiagnostics = diagnostics;
      this._audioLifecycle = attachAudioContextLifecycle(context);

      // 4. Create WASM engine instance via Embind
      const config: EngineConfig = this._buildEngineConfig();

      // EMSCRIPTEN_BINDINGS exports Embind classes directly on the module.
      this.enginePtr = new this.module.RbsAudioEngine();
      this.enginePtr.init(config);

      // 5. Register the JS AudioContext with Emscripten and create the worklet
      const contextHandle = this.module.emscriptenRegisterAudioObject(this.audioContext);

      await new Promise<void>((resolve, reject) => {
        this.module?.initAudioWorklet(contextHandle, this.enginePtr!, (nodeHandle) => {
          if (nodeHandle == null || nodeHandle === 0) {
            reject(
              new WasmInitError({
                reason: 'worklet-init-failed',
                message: INIT_FAILURE_MESSAGES['worklet-init-failed'],
              })
            );
            return;
          }
          this.workletNode =
            this.module?.emscriptenGetAudioObject<AudioWorkletNode>(nodeHandle) ?? null;
          resolve();
        });
      });

      // 6. Connect worklet node to audio graph
      if (!this.workletNode) {
        throw new Error('AudioWorklet node was not created');
      }
      // GainNode stays at unity — master volume is applied in WASM (see
      // audio-module.config.ts).
      this.gainNode = this.audioContext.createGain();
      this.gainNode.gain.value = 1.0;
      this.workletNode.connect(this.gainNode);
      this.gainNode.connect(this.audioContext.destination);

      // Sync engine master volume with the UI default.
      this.enginePtr.setVolume(this.volume);

      // 7. Start position polling loop
      this._startPositionPolling();

      this._setStatus('ready');
    } catch (err) {
      console.error('[WasmAudioBridge] init failed:', err);
      this._setStatus('error');
      if (err instanceof WasmInitError) throw err;
      const diag = this._audioDiagnostics
        ? ` [${this._audioDiagnostics.sampleRate} Hz, hint=${this._audioDiagnostics.latencyHint}]`
        : '';
      throw new WasmInitError({
        reason: 'engine-init-failed',
        message: INIT_FAILURE_MESSAGES['engine-init-failed'] + diag,
        cause: err,
      });
    }
  }

  /**
   * Load and parse an .rbs file.
   *
   * @param buffer Raw ArrayBuffer of the .rbs file
   * @returns ParsedSong metadata for UI display
   */
  async loadRbsFile(buffer: ArrayBuffer): Promise<ParsedSong> {
    if (!this.module || !this.enginePtr) {
      throw new Error('Bridge not initialised. Call init() first.');
    }

    this._setStatus('loading');

    try {
      // Copy file into WASM heap. Parse + load stay in C++ so TRAK automation
      // is never stripped by the Embind ParsedSong round-trip.
      const byteLength = buffer.byteLength;
      const ptr = mallocCopy(this.module, new Uint8Array(buffer));
      if (ptr === 0) {
        const err: EngineError = {
          code: 'PARSE_ERROR',
          message: `Not enough WASM heap for a ${Math.round(byteLength / 1024)} KB song.`,
        };
        throw err;
      }
      try {
        const parsed = this.enginePtr.loadSongFromBytes(ptr, byteLength);
        if (parsed === undefined) {
          const err: EngineError = {
            code: 'PARSE_ERROR',
            message: this.enginePtr.lastParseError(),
          };
          throw err;
        }

        const song = toUiParsedSong(parsed);

        // Keep the source bytes so a bounce can rebuild this song offline.
        // A new song starts from its own knob values, so drop stale overrides.
        this._songBuffer = buffer.slice(0);
        this._songTitle = song.title;
        this._paramOverrides.clear();

        this._setStatus('ready');
        return song;
      } finally {
        this.module._free(ptr);
      }
    } catch (err) {
      console.error('[WasmAudioBridge] load failed:', err);
      this._setStatus('error');
      throw err;
    }
  }

  /**
   * Load a `.rbm` mod, replacing drum and oscillator sounds with its samples.
   *
   * The file is copied once into the WASM heap and decoded entirely in C++.
   * Sample and skin bytes never cross back into JavaScript — only a metadata
   * summary does — so a multi-megabyte mod costs one heap copy, not two.
   *
   * Slots the mod does not supply keep their procedural voices.
   *
   * @param buffer Raw ArrayBuffer of the .rbm file
   * @returns A summary of what was loaded
   */
  async loadRbmFile(buffer: ArrayBuffer): Promise<ParsedMod> {
    if (!this.module || !this.enginePtr) {
      throw new Error('Bridge not initialised. Call init() first.');
    }

    this._setStatus('loading');

    const byteLength = buffer.byteLength;
    let ptr = 0;
    try {
      // The shipping build runs with ALLOW_MEMORY_GROWTH=0, so a large mod
      // can legitimately fail to allocate. Surface that as a real error
      // rather than writing to address 0.
      ptr = mallocCopy(this.module, new Uint8Array(buffer));
      if (ptr === 0) {
        const err: EngineError = {
          code: 'MOD_TOO_LARGE',
          message: `Not enough WASM heap for a ${Math.round(byteLength / 1024)} KB mod.`,
        };
        throw err;
      }

      const status = this.enginePtr.loadMod(ptr, byteLength);
      const report = toUiParsedMod(this.enginePtr.getModReport());

      if (report.loadedSlots === 0) {
        const err: EngineError = {
          code: 'MOD_LOAD_ERROR',
          message: this._modStatusMessage(MOD_LOAD_STATUSES[status] ?? 'no-samples'),
        };
        throw err;
      }

      // Kept so an offline bounce loads the same samples you are hearing.
      this._modBuffer = buffer.slice(0);

      this._setStatus('ready');
      return report;
    } catch (err) {
      console.error('[WasmAudioBridge] mod load failed:', err);
      this._setStatus('error');
      throw err;
    } finally {
      if (ptr !== 0) this.module._free(ptr);
    }
  }

  /** Mods require the WASM sample engine, which this bridge provides. */
  canLoadMod(): boolean {
    return true;
  }

  /** This bridge can bounce offline; the degraded player cannot. */
  canBounce(): boolean {
    return this.module !== null && this._songBuffer !== null;
  }

  /**
   * The flat EngineConfig the C++ side expects, per CONTRACT.md.
   *
   * Shared by init() and the offline bounce engine so the two can never
   * drift — a bounce configured differently from the live engine would not
   * be the same render.
   */
  private _buildEngineConfig(): EngineConfig {
    return {
      sampleRate: this.audioContext?.sampleRate ?? wasmAudioConfig.preferredSampleRate,
      bufferSize: wasmAudioConfig.bufferSize,
      enableTb303A: wasmAudioConfig.features.tb303_a,
      enableTb303B: wasmAudioConfig.features.tb303_b,
      enableTr808: wasmAudioConfig.features.tr808,
      enableTr909: wasmAudioConfig.features.tr909,
      enableDistortion: wasmAudioConfig.features.distortion,
      enableCompressor: wasmAudioConfig.features.compressor,
      enableDelay: wasmAudioConfig.features.delay,
    };
  }

  /** The per-render payload the Worker needs to rebuild what is playing. */
  private _bouncePayload(frames: number | undefined, deviceIndex: number): BouncePayload {
    if (!this._songBuffer) {
      throw { code: 'PARSE_ERROR', message: 'Nothing to render' } satisfies EngineError;
    }
    this._audioLifecycle?.resumeIfNeeded();
    return {
      songBytes: this._songBuffer.slice(0),
      modBytes: this._modBuffer ? this._modBuffer.slice(0) : null,
      paramOverrides: [...this._paramOverrides.values()],
      frames: frames && frames > 0 ? frames : 0,
      deviceIndex,
      tempo: this.enginePtr?.getTempo() ?? 125,
      config: this._buildEngineConfig(),
      glueScriptPath: wasmAudioConfig.glueScriptPath,
      wasmPath: wasmAudioConfig.wasmPath,
      workletPath: wasmAudioConfig.workletPath,
    };
  }

  /** Render the loaded song to a WAV file on a Worker (AudioContext-free). */
  async bounceToWav(frames?: number, deviceIndex: number = MASTER_BUS): Promise<RenderedFile> {
    return this._bounce.renderWav(this._bouncePayload(frames, deviceIndex), this._downloadStem());
  }

  /** Render one WAV per device on a single Worker engine. */
  async bounceStems(frames?: number): Promise<RenderedFile[]> {
    return this._bounce.renderStems(this._bouncePayload(frames, MASTER_BUS), this._downloadStem());
  }

  /** Frames covering the whole arrangement, for a default bounce length. */
  songLengthFrames(): number {
    return this.enginePtr?.songLengthFrames() ?? 0;
  }

  /** Filesystem-safe base name for downloads, from the song title. */
  private _downloadStem(): string {
    const title = (this._songTitle || 'rebirth-song').toLowerCase();
    const slug = title
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 48);
    return slug || 'rebirth-song';
  }

  /** Drop mod samples and return every voice to procedural synthesis. */
  clearMod(): void {
    this.enginePtr?.clearMod();
    this._modBuffer = null;
  }

  /** True when mod samples are currently backing at least one slot. */
  hasMod(): boolean {
    return this.enginePtr?.hasMod() ?? false;
  }

  /** Start or resume playback. */
  play(): void {
    if (!this.audioContext || !this.enginePtr) return;
    this._audioLifecycle?.resumeIfNeeded();
    this.enginePtr.play();
    this._setStatus('playing');
  }

  /** Pause playback. */
  pause(): void {
    if (!this.enginePtr) return;
    this.enginePtr.pause();
    this._setStatus('ready');
  }

  /** Stop playback and reset to bar 1. */
  stop(): void {
    if (!this.enginePtr) return;
    this.enginePtr.stop();
    this._setStatus('ready');
  }

  /** Seek to a specific bar (1-based). */
  seek(bar: number): void {
    if (!this.enginePtr) return;
    this.enginePtr.seek(bar);
  }

  /** Set master output volume (0.0–1.0). */
  setVolume(level: number): void {
    const clamped = Math.max(0, Math.min(1, Number.isFinite(level) ? level : this.volume));
    this.volume = clamped;
    this.enginePtr?.setVolume(clamped);
  }

  /** Return true if the WASM engine exposes tempo control hooks. */
  canSetTempo(): boolean {
    return !!(this.enginePtr?.setTempo && this.enginePtr?.getTempo);
  }

  /**
   * Read the engine's current absolute tempo in BPM.
   * Returns null when the engine or the tempo API is unavailable.
   */
  getTempoBpm(): number | null {
    if (!this.enginePtr?.getTempo) return null;
    return this.enginePtr.getTempo();
  }

  /**
   * Set tempo in BPM.
   * Returns false when the engine is not available.
   */
  setTempoBpm(bpm: number): boolean {
    if (!this.enginePtr) return false;
    const safeBpm = Math.max(40, Math.min(250, Math.round(bpm)));
    this.enginePtr.setTempo(safeBpm);
    return true;
  }

  /**
   * Set a tempo multiplier (e.g. 0.5 = half speed, 2.0 = double speed).
   * Returns false when the engine is not available.
   */
  /**
   * Live device/mixer parameter (0–1). Session-only; no file write.
   * deviceId: 0=303A 1=303B 2=808 3=909
   */
  setDeviceParam(deviceId: number, paramId: number, value: number): boolean {
    if (!this.enginePtr || typeof this.enginePtr.setDeviceParam !== 'function') return false;
    const id = Math.max(0, Math.min(3, Math.floor(deviceId)));
    const param = Math.max(0, Math.min(9, Math.floor(paramId)));
    const clamped = Math.max(0, Math.min(1, Number.isFinite(value) ? value : 0));
    this.enginePtr.setDeviceParam(id, param, clamped);
    // Remembered so an offline bounce reproduces what you are hearing, not
    // the song's untouched knob values. Live knob moves are session-only and
    // are never written back into the loaded song by the engine.
    this._paramOverrides.set(`${id}:${param}`, { deviceId: id, paramId: param, value: clamped });
    return true;
  }

  /**
   * Toggle or rewrite one pattern step in the engine's working copy.
   *
   * One patch per edit — the whole point of the C++-side `setStep`. Nothing
   * here sends a `ParsedSong` back across Embind: that copy cannot carry
   * TRAK automation, so a round trip would drop it on the first click.
   *
   * Session-only, like `setDeviceParam`: the `.rbs` bytes are untouched, so
   * reloading the file from disk restores the original pattern.
   */
  setStep(
    deviceId: number,
    bank: number,
    patternIndex: number,
    stepIndex: number,
    step: WasmStepData
  ): boolean {
    if (!this.enginePtr || typeof this.enginePtr.setStep !== 'function') return false;
    const coords = this._stepCoords(deviceId, bank, patternIndex, stepIndex);
    if (!coords) return false;
    return Boolean(
      this.enginePtr.setStep(
        coords.deviceId,
        coords.bank,
        coords.patternIndex,
        coords.stepIndex,
        {
          active: Boolean(step.active),
          note: Math.max(0, Math.min(127, Math.floor(step.note ?? 0))),
          drumExtra: Math.max(0, Math.min(255, Math.floor(step.drumExtra ?? 0))),
          accent: Boolean(step.accent),
          slide: Boolean(step.slide),
        }
      )
    );
  }

  /** Read a step back out of the engine's working copy, or null without an engine. */
  getStep(
    deviceId: number,
    bank: number,
    patternIndex: number,
    stepIndex: number
  ): WasmStepData | null {
    if (!this.enginePtr || typeof this.enginePtr.getStep !== 'function') return null;
    const coords = this._stepCoords(deviceId, bank, patternIndex, stepIndex);
    if (!coords) return null;
    return this.enginePtr.getStep(
      coords.deviceId,
      coords.bank,
      coords.patternIndex,
      coords.stepIndex
    );
  }

  /** Set a pattern's play length (1–16) in the engine's working copy. */
  setPatternLength(
    deviceId: number,
    bank: number,
    patternIndex: number,
    length: number
  ): boolean {
    if (!this.enginePtr || typeof this.enginePtr.setPatternLength !== 'function') return false;
    const coords = this._stepCoords(deviceId, bank, patternIndex, 0);
    if (!coords) return false;
    const steps = Math.floor(length);
    if (!Number.isFinite(steps) || steps < 1 || steps > 16) return false;
    return Boolean(
      this.enginePtr.setPatternLength(coords.deviceId, coords.bank, coords.patternIndex, steps)
    );
  }

  /**
   * Validate an edit coordinate before it crosses into WASM.
   *
   * The engine rejects out-of-range slots too, but `uint8_t` arguments wrap
   * on the way in — so a stray 256 would silently become device 0 rather
   * than being refused. Checking here keeps the boundary honest.
   */
  private _stepCoords(
    deviceId: number,
    bank: number,
    patternIndex: number,
    stepIndex: number
  ): { deviceId: number; bank: number; patternIndex: number; stepIndex: number } | null {
    const coords = {
      deviceId: Math.floor(deviceId),
      bank: Math.floor(bank),
      patternIndex: Math.floor(patternIndex),
      stepIndex: Math.floor(stepIndex),
    };
    if (!Object.values(coords).every((v) => Number.isFinite(v))) return null;
    if (coords.deviceId < 0 || coords.deviceId > 3) return null;
    if (coords.bank < 0 || coords.bank > 3) return null;
    if (coords.patternIndex < 0 || coords.patternIndex > 7) return null;
    if (coords.stepIndex < 0 || coords.stepIndex > 15) return null;
    return coords;
  }

  setTempoMultiplier(multiplier: number): boolean {
    if (!this.enginePtr) return false;
    const safeMultiplier = Math.max(
      0.25,
      Math.min(4, Number.isFinite(multiplier) ? multiplier : 1)
    );
    this.enginePtr.setTempoMultiplier(safeMultiplier);
    return true;
  }

  /** Clean up resources. */
  dispose(): void {
    this.stop();
    this._audioLifecycle?.dispose();
    this._audioLifecycle = null;
    this._bounce.dispose('Bridge disposed');
    if (this.positionPollId != null) {
      cancelAnimationFrame(this.positionPollId);
      this.positionPollId = null;
    }
    this.workletNode?.disconnect();
    this.gainNode?.disconnect();
    void this.audioContext?.close();
    this.enginePtr?.delete();
    this.enginePtr = null;
    this.module = null;
    this._setStatus('idle');
  }

  // ── Private helpers ─────────────────────────────────────────────

  private _setStatus(s: PlayerStatus) {
    this.status = s;
    this.onStatus?.(s);
  }

  private _startPositionPolling() {
    if (this.positionPollId != null) {
      cancelAnimationFrame(this.positionPollId);
      this.positionPollId = null;
    }
    const poll = () => {
      this.positionPollId = null;
      if (!this.enginePtr) {
        return;
      }
      if (this.status === 'playing') {
        const pos: PlaybackPosition = this.enginePtr.getPlaybackPosition();
        this.onPosition?.(pos.bar, pos.step);
      }
      this.positionPollId = requestAnimationFrame(poll);
    };
    this.positionPollId = requestAnimationFrame(poll);
  }

  /** Human-readable copy for a failed mod load, for the LCD / toast. */
  private _modStatusMessage(status: string): string {
    switch (status) {
      case 'arena-exhausted':
        return 'Mod is too large for the sample memory budget.';
      case 'no-samples':
        return 'No playable samples in this mod (it may be skins only).';
      case 'not-initialised':
        return 'Audio engine is not ready yet.';
      default:
        return 'Mod could not be loaded.';
    }
  }
}
