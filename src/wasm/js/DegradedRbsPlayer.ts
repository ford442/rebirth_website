/**
 * Degraded-mode player used when the WASM audio engine is unavailable.
 *
 * Provides:
 *   - Metadata-only display via RbsMetadataSniffer
 *   - Sketch playback: metronome clicks + step animation (audible, not silent)
 */

import type { ParsedSong, PlayerStatus } from '../types/wasm-audio';
import type { InitFailureReason } from './rbs-init-errors';
import { sniffRbsMetadata } from './RbsMetadataSniffer';

export type PositionCallback = (bar: number, step: number) => void;
export type StatusCallback = (status: PlayerStatus) => void;

export interface DegradedPlayerOptions {
  failureReason: InitFailureReason;
}

const SKETCH_STEPS = 16;

export class DegradedRbsPlayer {
  private status: PlayerStatus = 'idle';
  private onPosition: PositionCallback | null = null;
  private onStatus: StatusCallback | null = null;
  private volume = 0.8;
  private tempoBpm = 125;
  private audioContext: AudioContext | null = null;
  private gainNode: GainNode | null = null;
  private sketchTimer: ReturnType<typeof setInterval> | null = null;
  private currentStep = 0;
  private currentBar = 1;
  private loadedSong: ParsedSong | null = null;
  readonly failureReason: InitFailureReason;
  readonly mode: 'metadata' | 'sketch';

  constructor(options: DegradedPlayerOptions) {
    this.failureReason = options.failureReason;
    this.mode = this._canSketch() ? 'sketch' : 'metadata';
  }

  get playerStatus(): PlayerStatus {
    return this.status;
  }

  get isReady(): boolean {
    return this.status === 'ready' || this.status === 'playing';
  }

  get outputVolume(): number {
    return this.volume;
  }

  get canPlayAudio(): boolean {
    return this.mode === 'sketch';
  }

  setPositionCallback(cb: PositionCallback | null) {
    this.onPosition = cb;
  }

  setStatusCallback(cb: StatusCallback | null) {
    this.onStatus = cb;
  }

  async init(): Promise<void> {
    this._setStatus('loading');

    if (this.mode === 'sketch') {
      try {
        this.audioContext = new AudioContext();
        this.gainNode = this.audioContext.createGain();
        this.gainNode.gain.value = this.volume;
        this.gainNode.connect(this.audioContext.destination);
      } catch (err) {
        console.warn('[DegradedRbsPlayer] AudioContext unavailable:', err);
      }
    }

    this._setStatus('ready');
  }

  async loadRbsFile(buffer: ArrayBuffer): Promise<ParsedSong> {
    this._setStatus('loading');

    const meta = sniffRbsMetadata(buffer);
    if (!meta.valid && !meta.title) {
      const err = new Error(meta.error || 'Could not read .rbs metadata');
      this._setStatus('error');
      throw err;
    }

    this.tempoBpm = meta.bpm;
    this.currentStep = 0;
    this.currentBar = 1;

    const song: ParsedSong = {
      title: meta.title,
      author: meta.author,
      bpm: meta.bpm,
      devices: [],
      patterns: [],
      arrangement: [],
    };

    this.loadedSong = song;
    this._setStatus('ready');
    return song;
  }

  play(): void {
    if (!this.loadedSong) {
      this._setStatus('error');
      return;
    }

    if (this.mode !== 'sketch' || !this.audioContext || !this.gainNode) {
      // Metadata-only: never pretend playback succeeded silently.
      this._setStatus('error');
      return;
    }

    if (this.audioContext.state === 'suspended') {
      void this.audioContext.resume();
    }

    this._startSketch();
    this._setStatus('playing');
  }

  pause(): void {
    this._stopSketch();
    if (this.loadedSong) {
      this._setStatus('ready');
    }
  }

  stop(): void {
    this._stopSketch();
    this.currentStep = 0;
    this.currentBar = 1;
    this.onPosition?.(this.currentBar, this.currentStep);
    if (this.loadedSong) {
      this._setStatus('ready');
    }
  }

  setVolume(level: number): void {
    const clamped = Math.max(0, Math.min(1, Number.isFinite(level) ? level : this.volume));
    this.volume = clamped;
    if (this.gainNode) this.gainNode.gain.value = clamped;
  }

  canSetTempo(): boolean {
    return true;
  }

  getTempoBpm(): number | null {
    return this.tempoBpm;
  }

  setTempoBpm(bpm: number): boolean {
    const safeBpm = Math.max(40, Math.min(250, Math.round(bpm)));
    this.tempoBpm = safeBpm;
    if (this.status === 'playing') {
      this._stopSketch();
      this._startSketch();
    }
    return true;
  }

  dispose(): void {
    this._stopSketch();
    void this.audioContext?.close();
    this.audioContext = null;
    this.gainNode = null;
    this._setStatus('idle');
  }

  private _canSketch(): boolean {
    return typeof AudioContext !== 'undefined' || typeof (window as unknown as { webkitAudioContext?: unknown }).webkitAudioContext !== 'undefined';
  }

  private _setStatus(status: PlayerStatus) {
    this.status = status;
    this.onStatus?.(status);
  }

  private _startSketch() {
    this._stopSketch();
    const msPerStep = (60_000 / this.tempoBpm) / 4;
    this.sketchTimer = setInterval(() => {
      this._playStepClick(this.currentStep);
      this.onPosition?.(this.currentBar, this.currentStep);
      this.currentStep += 1;
      if (this.currentStep >= SKETCH_STEPS) {
        this.currentStep = 0;
        this.currentBar += 1;
      }
    }, msPerStep);
    this._playStepClick(this.currentStep);
    this.onPosition?.(this.currentBar, this.currentStep);
  }

  private _stopSketch() {
    if (this.sketchTimer != null) {
      clearInterval(this.sketchTimer);
      this.sketchTimer = null;
    }
  }

  private _playStepClick(step: number) {
    if (!this.audioContext || !this.gainNode) return;

    const ctx = this.audioContext;
    const now = ctx.currentTime;
    const isBeat = step % 4 === 0;

    const osc = ctx.createOscillator();
    const env = ctx.createGain();
    osc.type = isBeat ? 'square' : 'triangle';
    osc.frequency.value = isBeat ? 880 : 440;
    env.gain.setValueAtTime(0.0001, now);
    env.gain.exponentialRampToValueAtTime(isBeat ? 0.18 * this.volume : 0.08 * this.volume, now + 0.005);
    env.gain.exponentialRampToValueAtTime(0.0001, now + (isBeat ? 0.06 : 0.04));

    osc.connect(env);
    env.connect(this.gainNode);
    osc.start(now);
    osc.stop(now + 0.08);
  }
}
