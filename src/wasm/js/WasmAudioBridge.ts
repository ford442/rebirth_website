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
  ParsedSong,
  ParsedMod,
  RenderedFile,
  EngineConfig,
  PlayerStatus,
  EngineError,
  PlaybackPosition,
  EngineModule,
  RbsAudioEngineInstance,
} from '../types/wasm-audio';

import {
  MOD_LOAD_STATUSES,
  MASTER_BUS,
  STEM_DEVICES,
} from '../types/wasm-audio';

import { wasmAudioConfig } from '../audio-module.config';
import type { AudioContextDiagnostics } from '../types/wasm-audio';
import { toUiParsedMod, toUiParsedSong } from '../types/wasm-audio-mapping';
import { WasmInitError, INIT_FAILURE_MESSAGES } from './rbs-init-errors';
import { createProductionAudioContext } from './create-audio-context';
import { waitForCrossOriginIsolation } from '../../scripts/coi-bootstrap';

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

  // Kept so an offline bounce can rebuild exactly what is playing: the source
  // files plus any live knob moves, which the engine treats as session-only.
  private _songBuffer: ArrayBuffer | null = null;
  private _modBuffer: ArrayBuffer | null = null;
  private _songTitle = '';
  private _paramOverrides = new Map<string, { deviceId: number; paramId: number; value: number }>();

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
        locateFile: (path: string) => {
          if (path.endsWith('.wasm')) return wasmAudioConfig.wasmPath;
          // Map both the legacy .aw.js sidecar and Emscripten 6's rewritten
          // module request to the stable worklet path produced by build.sh.
          if (path.endsWith('rbsWorklet.js') || (path.endsWith('.js') && path.includes('.aw.'))) {
            return wasmAudioConfig.workletPath;
          }
          return path;
        },
      });

      // 3. Create AudioContext (retry without sampleRate on NotSupportedError)
      const { context, diagnostics } = createProductionAudioContext(
        wasmAudioConfig.preferredSampleRate,
        wasmAudioConfig.latencyHint
      );
      this.audioContext = context;
      this._audioDiagnostics = diagnostics;

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
      // Copy file into WASM heap
      const byteLength = buffer.byteLength;
      const ptr = this.module._malloc(byteLength);
      const parser = new this.module.RbsParser();
      try {
        this.module.HEAPU8.set(new Uint8Array(buffer), ptr);

        // Parse via Embind-exposed parser
        const parsed = parser.parse(ptr, byteLength);
        if (parsed === undefined) {
          const err: EngineError = {
            code: 'PARSE_ERROR',
            message: parser.lastError(),
          };
          throw err;
        }

        // Load into engine before consuming the Embind vector handles.
        this.enginePtr.loadSong(parsed);
        const song = toUiParsedSong(parsed);

        // Keep the source bytes so a bounce can rebuild this song offline.
        // A new song starts from its own knob values, so drop stale overrides.
        this._songBuffer = buffer.slice(0);
        this._songTitle = song.title;
        this._paramOverrides.clear();

        this._setStatus('ready');
        return song;
      } finally {
        parser.delete();
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
      ptr = this.module._malloc(byteLength);
      if (ptr === 0) {
        const err: EngineError = {
          code: 'MOD_TOO_LARGE',
          message: `Not enough WASM heap for a ${Math.round(byteLength / 1024)} KB mod.`,
        };
        throw err;
      }

      this.module.HEAPU8.set(new Uint8Array(buffer), ptr);

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

  /**
   * Build a throwaway engine loaded with exactly what the live one is
   * playing, for offline rendering.
   *
   * A bounce must not run on the live engine: renderOffline() drives the
   * sequencer from the calling thread, and the AudioWorklet is driving the
   * same sequencer from its own. Rendering into a second engine sidesteps
   * that entirely — no locks, no stopping playback, and the listener does
   * not hear a gap while a file is written.
   */
  private _createOfflineEngine(): RbsAudioEngineInstance | null {
    if (!this.module || !this._songBuffer) return null;

    const engine = new this.module.RbsAudioEngine();
    engine.init(this._buildEngineConfig());

    const songBytes = new Uint8Array(this._songBuffer);
    const songPtr = this.module._malloc(songBytes.byteLength);
    if (songPtr === 0) {
      engine.delete();
      return null;
    }

    const parser = new this.module.RbsParser();
    try {
      this.module.HEAPU8.set(songBytes, songPtr);
      const parsed = parser.parse(songPtr, songBytes.byteLength);
      if (parsed === undefined) {
        engine.delete();
        return null;
      }
      engine.loadSong(parsed);
    } finally {
      parser.delete();
      this.module._free(songPtr);
    }

    // Re-apply the mod, so a bounce uses the same samples you are hearing.
    if (this._modBuffer) {
      const modBytes = new Uint8Array(this._modBuffer);
      const modPtr = this.module._malloc(modBytes.byteLength);
      if (modPtr !== 0) {
        try {
          this.module.HEAPU8.set(modBytes, modPtr);
          engine.loadMod(modPtr, modBytes.byteLength);
        } finally {
          this.module._free(modPtr);
        }
      }
    }

    // …and the live knob moves, which are session-only in the engine.
    for (const override of this._paramOverrides.values()) {
      engine.setDeviceParam(override.deviceId, override.paramId, override.value);
    }

    engine.setTempo(this.enginePtr?.getTempo() ?? 125);
    return engine;
  }

  /** Frames covering the whole arrangement, for a default bounce length. */
  songLengthFrames(): number {
    return this.enginePtr?.songLengthFrames() ?? 0;
  }

  /**
   * Render the loaded song to a WAV file.
   *
   * @param frames Defaults to the full arrangement.
   * @param deviceIndex A stem index, or MASTER_BUS for the full mix.
   */
  bounceToWav(frames?: number, deviceIndex: number = MASTER_BUS): RenderedFile | null {
    const engine = this._createOfflineEngine();
    if (!engine) return null;

    try {
      const length = frames && frames > 0 ? frames : engine.songLengthFrames();
      if (length === 0) return null;

      const bytes = engine.renderOfflineToWav(length, deviceIndex);
      const stem = STEM_DEVICES.find((d) => d.index === deviceIndex);
      const suffix = stem ? `-${stem.slug}` : '';
      return {
        filename: `${this._downloadStem()}${suffix}.wav`,
        bytes,
        mimeType: 'audio/wav',
      };
    } finally {
      engine.delete();
    }
  }

  /**
   * Render one WAV per device.
   *
   * Uses a single offline engine for all four passes rather than rebuilding
   * it each time, so a stem set costs one parse instead of four.
   */
  bounceStems(frames?: number): RenderedFile[] {
    const engine = this._createOfflineEngine();
    if (!engine) return [];

    try {
      const length = frames && frames > 0 ? frames : engine.songLengthFrames();
      if (length === 0) return [];

      const files: RenderedFile[] = [];
      for (const device of STEM_DEVICES) {
        const bytes = engine.renderOfflineToWav(length, device.index);
        files.push({
          filename: `${this._downloadStem()}-${device.slug}.wav`,
          bytes,
          mimeType: 'audio/wav',
        });
      }
      return files;
    } finally {
      engine.delete();
    }
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
  }

  /** True when mod samples are currently backing at least one slot. */
  hasMod(): boolean {
    return this.enginePtr?.hasMod() ?? false;
  }

  /** Start or resume playback. */
  play(): void {
    if (!this.audioContext || !this.enginePtr) return;
    if (this.audioContext.state === 'suspended') {
      void this.audioContext.resume();
    }
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
