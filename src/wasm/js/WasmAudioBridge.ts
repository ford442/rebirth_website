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
} from '../types/wasm-audio';

import { wasmAudioConfig } from '../audio-module.config.js';

/** Callback invoked when playback position changes (bar, step). */
export type PositionCallback = (bar: number, step: number) => void;

/** Callback invoked when player status changes. */
export type StatusCallback = (status: PlayerStatus) => void;

export class WasmAudioBridge {
  private module: any = null;
  private audioContext: AudioContext | null = null;
  private workletNode: AudioWorkletNode | null = null;
  private enginePtr: any = null;
  private status: PlayerStatus = 'idle';
  private onPosition: PositionCallback | null = null;
  private onStatus: StatusCallback | null = null;

  /** Is the bridge initialised and ready to load files? */
  get isReady(): boolean {
    return this.status === 'ready' || this.status === 'playing';
  }

  /** Current player status. */
  get playerStatus(): PlayerStatus {
    return this.status;
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
      if (!this._checkSupport()) {
        throw new Error('Browser does not support required audio APIs');
      }

      // 2. Load Emscripten glue (dynamic import of the generated JS)
      const glueModule = await import(/* @vite-ignore */ wasmAudioConfig.glueScriptPath);
      this.module = await glueModule.default({
        locateFile: (path: string) => {
          if (path.endsWith('.wasm')) return wasmAudioConfig.wasmPath;
          if (path.endsWith('.js') && path.includes('ww')) return wasmAudioConfig.workletPath;
          return path;
        },
      });

      // 3. Create AudioContext
      this.audioContext = new AudioContext({
        sampleRate: wasmAudioConfig.sampleRate,
      });

      // 4. Load AudioWorklet processor
      await this.audioContext.audioWorklet.addModule(wasmAudioConfig.workletPath);

      // 5. Create WASM engine instance via Embind
      const config: EngineConfig = {
        sampleRate: this.audioContext.sampleRate,
        bufferSize: wasmAudioConfig.bufferSize,
        features: wasmAudioConfig.features as EngineConfig['features'],
      };

      // Engine is exposed via embind as rb338.RbsAudioEngine
      this.enginePtr = new this.module.rb338.RbsAudioEngine();
      this.enginePtr.init(config);

      // 6. Connect worklet node to audio graph
      this.workletNode = new AudioWorkletNode(
        this.audioContext,
        'rbs-player',
        {
          numberOfInputs: 0,
          numberOfOutputs: 1,
          outputChannelCount: [2],
          processorOptions: {
            wasmModule: this.module,
            enginePtr: this.enginePtr,
            sampleRate: this.audioContext.sampleRate,
          },
        }
      );

      this.workletNode.connect(this.audioContext.destination);

      // 7. Start position polling loop
      this._startPositionPolling();

      this._setStatus('ready');
    } catch (err) {
      console.error('[WasmAudioBridge] init failed:', err);
      this._setStatus('error');
      throw err;
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
      this.module.HEAPU8.set(new Uint8Array(buffer), ptr);

      // Parse via Embind-exposed parser
      const parser = new this.module.rb338.RbsParser();
      const parsed = parser.parse(ptr, byteLength);

      // Free file buffer
      this.module._free(ptr);

      if (!parsed) {
        const err: EngineError = {
          code: 'PARSE_ERROR',
          message: parser.lastError(),
        };
        throw err;
      }

      // Load into engine
      this.enginePtr.loadSong(parsed);

      // Extract JS-friendly metadata
      const song: ParsedSong = {
        title: parsed.title,
        author: parsed.author,
        bpm: parsed.bpm,
        devices: [],
        patterns: [],
        arrangement: [],
      };

      this._setStatus('ready');
      return song;
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
      this.audioContext.resume();
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

  /** Clean up resources. */
  dispose(): void {
    this.stop();
    this.workletNode?.disconnect();
    this.audioContext?.close();
    this.enginePtr?.delete?.();
    this.module = null;
    this._setStatus('idle');
  }

  // ── Private helpers ─────────────────────────────────────────────

  private _checkSupport(): boolean {
    const hasWorklet = typeof AudioWorkletNode !== 'undefined';
    const hasWasm = typeof WebAssembly === 'object';
    return hasWorklet && hasWasm;
  }

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
      // getPlaybackPosition writes to out-params via embind
      const pos = this.enginePtr.getPlaybackPosition();
      this.onPosition?.(pos.bar, pos.step);
      requestAnimationFrame(poll);
    };
    requestAnimationFrame(poll);
  }
}
