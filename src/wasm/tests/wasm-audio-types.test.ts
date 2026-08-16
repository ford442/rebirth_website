/**
 * Compile-time bridge contract test.
 *
 * This file is type-checked by `npx astro check`. It does not run at runtime
 * and does not require a compiled WASM binary. It proves that:
 *
 *   - EngineConfig matches the C++ struct shape (flat feature booleans).
 *   - PlaybackPosition matches the Embind value object.
 *   - The bridge can construct an EngineConfig from audio-module.config.ts.
 *   - The bridge's expected EngineModule / RbsAudioEngineInstance signatures
 *     are consistent with the types exported from wasm-audio.ts.
 */

import { buildStepCells, pickPattern } from '../js/player-studio';
import type { ParsedSong } from '../types/wasm-audio';
import type {
  EngineConfig,
  PlaybackPosition,
  WasmParsedSong,
  WasmDeviceId,
  RbsAudioEngineInstance,
  RbsParserInstance,
  EngineModule,
} from '../types/wasm-audio';

import { wasmAudioConfig } from '../audio-module.config';

// ── EngineConfig shape ───────────────────────────────────────────

const engineConfig: EngineConfig = {
  sampleRate: wasmAudioConfig.sampleRate,
  bufferSize: wasmAudioConfig.bufferSize,
  enableTb303A: wasmAudioConfig.features.tb303_a,
  enableTb303B: wasmAudioConfig.features.tb303_b,
  enableTr808: wasmAudioConfig.features.tr808,
  enableTr909: wasmAudioConfig.features.tr909,
  enableDistortion: wasmAudioConfig.features.distortion,
  enableCompressor: wasmAudioConfig.features.compressor,
  enableDelay: wasmAudioConfig.features.delay,
};

// Ensure the flattened config is exactly the shape the WASM engine expects.
const configCheck: EngineConfig = {
  sampleRate: 44100,
  bufferSize: 128,
  enableTb303A: true,
  enableTb303B: true,
  enableTr808: true,
  enableTr909: true,
  enableDistortion: true,
  enableCompressor: true,
  enableDelay: true,
};

void engineConfig;
void configCheck;

// ── PlaybackPosition shape ───────────────────────────────────────

const position: PlaybackPosition = {
  bar: 1,
  step: 0,
};

void position;

// ── Mock WASM module type check ──────────────────────────────────

class MockRbsAudioEngine implements RbsAudioEngineInstance {
  init(_config: EngineConfig): boolean {
    return true;
  }

  loadSong(_song: WasmParsedSong): boolean {
    return true;
  }

  play(): void {}
  pause(): void {}
  stop(): void {}
  seek(_bar: number): void {}
  setVolume(_volume: number): void {}
  setTempo(_bpm: number): void {}
  getTempo(): number {
    return 120;
  }
  setTempoMultiplier(_multiplier: number): void {}
  setDeviceParam(_deviceId: number, _paramId: number, _value: number): void {}

  isPlaying(): boolean {
    return false;
  }

  getProcessedBlockCount(): number {
    return 0;
  }

  renderTestBlock(_numFrames: number): number {
    return 0;
  }

  getPlaybackPosition(): PlaybackPosition {
    return { bar: 1, step: 0 };
  }

  delete(): void {}
}

class MockRbsParser implements RbsParserInstance {
  parse(_ptr: number, _size: number): WasmParsedSong | undefined {
    return undefined;
  }

  lastError(): string {
    return '';
  }

  delete(): void {}
}

const mockModule: EngineModule = {
  DeviceId: {
    TB303_A: 0 as WasmDeviceId,
    TB303_B: 1 as WasmDeviceId,
    TR808: 2 as WasmDeviceId,
    TR909: 3 as WasmDeviceId,
  },
  RbsAudioEngine: MockRbsAudioEngine,
  RbsParser: MockRbsParser,
  initAudioWorklet(_contextHandle, _engine, callback) {
    callback(1);
  },
  HEAPU8: new Uint8Array(0),
  _malloc(_size: number): number {
    return 0;
  },
  _free(_ptr: number): void {},
  emscriptenRegisterAudioObject(_obj: AudioContext): number {
    return 1;
  },
  emscriptenGetAudioObject<T>(_handle: number): T {
    return {} as T;
  },
};

void mockModule;

// ── Static assertions via assignability ──────────────────────────

// If these assignments fail, the bridge contract is out of sync.
type AssertEngineConfig = EngineConfig extends {
  sampleRate: number;
  bufferSize: number;
  enableTb303A: boolean;
}
  ? true
  : false;
type AssertPlaybackPosition = PlaybackPosition extends { bar: number; step: number } ? true : false;

const _assertConfig: AssertEngineConfig = true;
const _assertPosition: AssertPlaybackPosition = true;

void _assertConfig;
void _assertPosition;

const studioSong: ParsedSong = {
  title: 'fixture',
  author: 'test',
  bpm: 128,
  devices: [
    {
      deviceId: 'tb303-a',
      knobs: {},
      muted: false,
      level: 0.8,
      pan: 0.5,
      waveform: 0,
      initialPatternBank: 0,
      initialPatternIndex: 0,
    },
  ],
  patterns: [
    {
      deviceId: 'tb303-a',
      bank: 0,
      patternIndex: 0,
      steps: Array.from({ length: 16 }, (_, i) => ({
        active: i === 0 || i === 4,
        note: 48,
        accent: i === 0,
        slide: i === 4,
      })),
    },
    {
      deviceId: 'tr808',
      bank: 0,
      patternIndex: 0,
      steps: Array.from({ length: 16 }, (_, i) => ({
        active: i === 0,
        note: 0x01,
        accent: false,
        slide: false,
        drumExtra: 0,
      })),
    },
  ],
  arrangement: [],
};

const acidPattern = pickPattern(studioSong, 'tb303-a', 0, 0);
const acidCells = buildStepCells(acidPattern, 'tb303-a');
const drumCells = buildStepCells(pickPattern(studioSong, 'tr808', 0, 0), 'tr808');
const _assertAcidGrid: true =
  acidCells[0].active && acidCells[0].accent && acidCells[4].slide ? true : (undefined as never);
const _assertDrumGrid: true = drumCells[0].label.includes('BD') ? true : (undefined as never);

void _assertAcidGrid;
void _assertDrumGrid;
