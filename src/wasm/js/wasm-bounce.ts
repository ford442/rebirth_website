/**
 * Offline bounce client — the main-thread half of the WAV/stem render.
 *
 * The render itself happens in `bounce-worker.ts`, which instantiates a
 * **second** copy of the WASM module. That keeps the live AudioWorklet engine
 * from ever holding two `SamplePool` arenas on the shipping 64 MiB heap.
 *
 * This module owns the Worker, the request sequence and the pending-response
 * map, so `WasmAudioBridge` only has to say *what* to render. Nothing here
 * touches the AudioContext or the live engine.
 *
 * The worker streams each file as fixed-size PCM chunks; this side writes the
 * WAV header and assembles the body (`WavAssembler`), so the file is only ever
 * whole in JavaScript memory — never in the fixed 64 MiB WASM heap.
 */

interface PendingBounce {
  files: WavAssembler[];
  resolve: (files: Uint8Array[]) => void;
  reject: (err: unknown) => void;
}

import type { BounceRequest, BounceResponse } from './bounce-protocol';
import type { EngineError, RenderedFile } from '../types/wasm-audio-engine';
import { STEM_DEVICES } from '../types/wasm-audio-engine';
import { WavAssembler } from './wav-assembler';

/** Everything the bridge must supply for one render, minus the framing. */
export type BouncePayload = Omit<BounceRequest, 'id' | 'kind'>;

export class BounceClient {
  private worker: Worker | null = null;
  private seq = 0;
  private pending = new Map<number, PendingBounce>();

  /** The worker's WASM heap peak during the last finished bounce (heap probe). */
  lastHeap: { peakBytes: number; heapSize: number } | null = null;

  /** Lazily spawns the render Worker; reused across bounces. */
  private ensureWorker(): Worker {
    if (this.worker) return this.worker;
    this.worker = new Worker(new URL('./bounce-worker.ts', import.meta.url), {
      type: 'module',
    });
    this.worker.onmessage = (event: MessageEvent<BounceResponse>) => {
      this.onMessage(event.data);
    };
    this.worker.onerror = (event) => {
      const err = new Error(event.message || 'Bounce worker failed');
      for (const pending of this.pending.values()) {
        pending.reject(err);
      }
      this.pending.clear();
    };
    return this.worker;
  }

  private onMessage(message: BounceResponse): void {
    const pending = this.pending.get(message.id);
    if (!pending) return;
    const fail = (err: unknown) => {
      this.pending.delete(message.id);
      pending.reject(err);
    };
    if (!message.ok) {
      fail({ code: message.error.code, message: message.error.message } satisfies EngineError);
      return;
    }
    try {
      switch (message.kind) {
        case 'begin':
          pending.files[message.file] = new WavAssembler(message.frames, message.sampleRate);
          return;
        case 'chunk': {
          const file = pending.files[message.file];
          if (!file) throw new Error('Bounce chunk arrived before its file began');
          file.append(new Uint8Array(message.data));
          return;
        }
        case 'done': {
          const files = pending.files;
          if (files.length !== message.files || !files.every((f) => f?.complete)) {
            throw new Error('Bounce ended with an incomplete file');
          }
          this.pending.delete(message.id);
          this.lastHeap = { peakBytes: message.peakHeapBytes, heapSize: message.heapSize };
          pending.resolve(files.map((f) => f.bytes));
          return;
        }
      }
    } catch (err) {
      fail(err);
    }
  }

  private request(payload: BouncePayload, kind: BounceRequest['kind']): Promise<Uint8Array[]> {
    const worker = this.ensureWorker();
    const request: BounceRequest = { ...payload, id: ++this.seq, kind };
    const transfer: Transferable[] = [request.songBytes];
    if (request.modBytes) transfer.push(request.modBytes);
    return new Promise((resolve, reject) => {
      this.pending.set(request.id, { files: [], resolve, reject });
      worker.postMessage(request, transfer);
    });
  }

  /** Render one WAV — a stem when `payload.deviceIndex` names a device. */
  async renderWav(payload: BouncePayload, baseName: string): Promise<RenderedFile> {
    const [bytes] = await this.request(payload, 'wav');
    const stem = STEM_DEVICES.find((d) => d.index === payload.deviceIndex);
    const suffix = stem ? `-${stem.slug}` : '';
    return {
      filename: `${baseName}${suffix}.wav`,
      bytes: bytes ?? new Uint8Array(0),
      mimeType: 'audio/wav',
    };
  }

  /** Render one WAV per device, on a single Worker engine. */
  async renderStems(payload: BouncePayload, baseName: string): Promise<RenderedFile[]> {
    const files = await this.request(payload, 'stems');
    return STEM_DEVICES.map((device, index) => ({
      filename: `${baseName}-${device.slug}.wav`,
      bytes: files[index] ?? new Uint8Array(0),
      mimeType: 'audio/wav',
    }));
  }

  /** Terminate the Worker and reject anything still in flight. */
  dispose(reason = 'Bounce client disposed'): void {
    this.worker?.terminate();
    this.worker = null;
    for (const pending of this.pending.values()) {
      pending.reject(new Error(reason));
    }
    this.pending.clear();
  }
}
