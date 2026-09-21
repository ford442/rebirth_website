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
 */

import type { BounceRequest, BounceResponse } from './bounce-protocol';
import type { EngineError, RenderedFile } from '../types/wasm-audio-engine';
import { STEM_DEVICES } from '../types/wasm-audio-engine';

/** Everything the bridge must supply for one render, minus the framing. */
export type BouncePayload = Omit<BounceRequest, 'id' | 'kind'>;

export class BounceClient {
  private worker: Worker | null = null;
  private seq = 0;
  private pending = new Map<
    number,
    { resolve: (value: BounceResponse) => void; reject: (err: Error) => void }
  >();

  /** Lazily spawns the render Worker; reused across bounces. */
  private ensureWorker(): Worker {
    if (this.worker) return this.worker;
    this.worker = new Worker(new URL('./bounce-worker.ts', import.meta.url), {
      type: 'module',
    });
    this.worker.onmessage = (event: MessageEvent<BounceResponse>) => {
      const pending = this.pending.get(event.data.id);
      if (!pending) return;
      this.pending.delete(event.data.id);
      pending.resolve(event.data);
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

  private post(request: BounceRequest): Promise<BounceResponse> {
    const worker = this.ensureWorker();
    const transfer: Transferable[] = [request.songBytes];
    if (request.modBytes) transfer.push(request.modBytes);
    return new Promise((resolve, reject) => {
      this.pending.set(request.id, { resolve, reject });
      worker.postMessage(request, transfer);
    });
  }

  private async request<K extends BounceRequest['kind']>(
    payload: BouncePayload,
    kind: K
  ): Promise<Extract<BounceResponse, { ok: true; kind: K }>> {
    const response = await this.post({ ...payload, id: ++this.seq, kind });
    if (!response.ok) {
      const err: EngineError = {
        code: response.error.code,
        message: response.error.message,
      };
      throw err;
    }
    if (response.kind !== kind) {
      throw new Error('Unexpected bounce response');
    }
    return response as Extract<BounceResponse, { ok: true; kind: K }>;
  }

  /** Render one WAV — a stem when `payload.deviceIndex` names a device. */
  async renderWav(payload: BouncePayload, baseName: string): Promise<RenderedFile> {
    const response = await this.request(payload, 'wav');
    const stem = STEM_DEVICES.find((d) => d.index === payload.deviceIndex);
    const suffix = stem ? `-${stem.slug}` : '';
    return {
      filename: `${baseName}${suffix}.wav`,
      bytes: new Uint8Array(response.wav),
      mimeType: 'audio/wav',
    };
  }

  /** Render one WAV per device, on a single Worker engine. */
  async renderStems(payload: BouncePayload, baseName: string): Promise<RenderedFile[]> {
    const response = await this.request(payload, 'stems');
    return STEM_DEVICES.map((device, index) => ({
      filename: `${baseName}-${device.slug}.wav`,
      bytes: new Uint8Array(response.wavs[index] ?? new ArrayBuffer(0)),
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
