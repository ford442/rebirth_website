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
  EngineConfig,
  PlayerStatus,
  EngineError,
  PlaybackPosition,
  WasmParsedSong,
  WasmDeviceState,
  WasmPattern,
  WasmArrangementBar,
  WasmDeviceId,
  EngineModule,
  RbsAudioEngineInstance,
} from '../types/wasm-audio';

import { wasmAudioConfig } from '../audio-module.config';
import {
  WasmInitError,
  INIT_FAILURE_MESSAGES,
} from './rbs-init-errors';
import { waitForCrossOriginIsolation } from '../../scripts/coi-bootstrap';

/** Callback invoked when playback position changes (bar, step). */
export type PositionCallback = (bar: number, step: number) => void;

/** Callback invoked when player status changes. */
export type StatusCallback = (status: PlayerStatus) => void;

const DEVICE_LABELS: Record<WasmDeviceId, string> = {
  0: 'tb303-a',
  1: 'tb303-b',
  2: 'tr808',
  3: 'tr909',
};

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
   *   3. Create AudioContext
   *   4. Add AudioWorklet module
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
          if (
            path.endsWith('rbsWorklet.js') ||
            (path.endsWith('.js') && path.includes('.aw.'))
          ) {
            return wasmAudioConfig.workletPath;
          }
          return path;
        },
      });

      // 3. Create AudioContext
      this.audioContext = new AudioContext({
        sampleRate: wasmAudioConfig.sampleRate,
      });

      // 4. Create WASM engine instance via Embind
      const config: EngineConfig = {
        sampleRate: this.audioContext.sampleRate,
        bufferSize: wasmAudioConfig.bufferSize,
        enableTb303A: wasmAudioConfig.features.tb303_a,
        enableTb303B: wasmAudioConfig.features.tb303_b,
        enableTr808: wasmAudioConfig.features.tr808,
        enableTr909: wasmAudioConfig.features.tr909,
        enableDistortion: wasmAudioConfig.features.distortion,
        enableCompressor: wasmAudioConfig.features.compressor,
        enableDelay: wasmAudioConfig.features.delay,
      };

      // EMSCRIPTEN_BINDINGS exports Embind classes directly on the module.
      this.enginePtr = new this.module.RbsAudioEngine();
      this.enginePtr.init(config);

      // 5. Register the JS AudioContext with Emscripten and create the worklet
      const contextHandle = this.module.emscriptenRegisterAudioObject(this.audioContext);

      await new Promise<void>((resolve, reject) => {
        this.module?.initAudioWorklet(
          contextHandle,
          this.enginePtr!,
          (nodeHandle) => {
            if (nodeHandle == null || nodeHandle === 0) {
              reject(
                new WasmInitError({
                  reason: 'worklet-init-failed',
                  message: INIT_FAILURE_MESSAGES['worklet-init-failed'],
                })
              );
              return;
            }
            this.workletNode = this.module?.emscriptenGetAudioObject<AudioWorkletNode>(nodeHandle) ?? null;
            resolve();
          }
        );
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
      throw new WasmInitError({
        reason: 'engine-init-failed',
        message: INIT_FAILURE_MESSAGES['engine-init-failed'],
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
        const song = this._toUiParsedSong(parsed);

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
  setTempoMultiplier(multiplier: number): boolean {
    if (!this.enginePtr) return false;
    const safeMultiplier = Math.max(0.25, Math.min(4, Number.isFinite(multiplier) ? multiplier : 1));
    this.enginePtr.setTempoMultiplier(safeMultiplier);
    return true;
  }

  /** Clean up resources. */
  dispose(): void {
    this.stop();
    this.workletNode?.disconnect();
    this.gainNode?.disconnect();
    void this.audioContext?.close();
    this.enginePtr?.delete();
    this.module = null;
    this._setStatus('idle');
  }

  // ── Private helpers ─────────────────────────────────────────────

  private _setStatus(s: PlayerStatus) {
    this.status = s;
    this.onStatus?.(s);
  }

  private _startPositionPolling() {
    const poll = () => {
      if (!this.enginePtr || this.status !== 'playing') {
        requestAnimationFrame(poll);
        return;
      }
      const pos: PlaybackPosition = this.enginePtr.getPlaybackPosition();
      this.onPosition?.(pos.bar, pos.step);
      requestAnimationFrame(poll);
    };
    requestAnimationFrame(poll);
  }

  private _toUiParsedSong(wasmSong: WasmParsedSong): ParsedSong {
    const patterns = this._consumeVector(wasmSong.patterns);
    const arrangement = this._consumeVector(wasmSong.arrangement);
    return {
      title: wasmSong.title,
      author: wasmSong.author,
      bpm: wasmSong.bpm,
      devices: wasmSong.devices.map((d) => this._toUiDeviceState(d)),
      patterns: patterns.map((p) => this._toUiPattern(p)),
      arrangement: arrangement.map((b) => this._toUiArrangementStep(b)),
    };
  }

  private _consumeVector<T>(vector: import('../types/wasm-audio').EmbindVector<T>): T[] {
    const values: T[] = [];
    try {
      for (let index = 0; index < vector.size(); index += 1) {
        values.push(vector.get(index));
      }
      return values;
    } finally {
      vector.delete();
    }
  }

  private _toUiDeviceState(wasmDevice: WasmDeviceState): import('../types/wasm-audio').DeviceState {
    return {
      deviceId: DEVICE_LABELS[wasmDevice.id] as 'tb303-a' | 'tb303-b' | 'tr808' | 'tr909',
      knobs: {
        tune: wasmDevice.tune,
        cutoff: wasmDevice.cutoff,
        resonance: wasmDevice.resonance,
        envMod: wasmDevice.envMod,
        decay: wasmDevice.decay,
        accent: wasmDevice.accent,
      },
      muted: wasmDevice.muted,
    };
  }

  private _toUiPattern(wasmPattern: WasmPattern): import('../types/wasm-audio').Pattern {
    return {
      deviceId: DEVICE_LABELS[wasmPattern.deviceId],
      bank: wasmPattern.bank,
      patternIndex: wasmPattern.patternIndex,
      steps: wasmPattern.steps.map((s) => ({
        active: s.active,
        note: s.note === 0 ? undefined : s.note,
        accent: s.accent,
        slide: s.slide,
      })),
    };
  }

  private _toUiArrangementStep(wasmBar: WasmArrangementBar): import('../types/wasm-audio').ArrangementStep {
    const patternRefs: Record<string, import('../types/wasm-audio').PatternRef> = {};
    wasmBar.devicePatterns.forEach((ref, index) => {
      const label = DEVICE_LABELS[index as WasmDeviceId];
      if (label) {
        patternRefs[label] = { bank: ref.bank, index: ref.index };
      }
    });
    return {
      bar: wasmBar.barNumber,
      patternRefs,
    };
  }
}
