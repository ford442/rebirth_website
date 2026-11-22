export interface BounceParamOverride {
  deviceId: number;
  paramId: number;
  value: number;
}

export interface BounceEngineFeatures {
  sampleRate: number;
  enableTb303A: boolean;
  enableTb303B: boolean;
  enableTr808: boolean;
  enableTr909: boolean;
}

export interface BounceRequest {
  id: number;
  kind: 'wav' | 'stems';
  songBytes: ArrayBuffer;
  modBytes: ArrayBuffer | null;
  paramOverrides: BounceParamOverride[];
  frames: number;
  deviceIndex: number;
  tempo: number;
  config: BounceEngineFeatures;
  glueScriptPath: string;
  wasmPath: string;
  workletPath: string;
  pthreadWorkerPath?: string;
}

export interface BounceErrorPayload {
  code: string;
  message: string;
}

/**
 * Worker → main messages for one request, in order:
 *
 *   for each file (1 for 'wav', 4 for 'stems'):
 *     `begin`  — frame count and rate, so the client allocates the WAV once
 *     `chunk`… — interleaved PCM16 bytes, transferred, at most
 *                OFFLINE_CHUNK_FRAMES frames each
 *   `done`     — every file is complete
 *
 * or a single `error` at any point. The WAV header is written on the main
 * thread; the WASM heap only ever holds one chunk.
 */
export type BounceResponse =
  | { id: number; ok: true; kind: 'begin'; file: number; frames: number; sampleRate: number }
  | { id: number; ok: true; kind: 'chunk'; file: number; data: ArrayBuffer }
  | {
      id: number;
      ok: true;
      kind: 'done';
      files: number;
      /** Peak allocator `usedBytes` in the worker's WASM heap during the render. */
      peakHeapBytes: number;
      /** The worker heap's fixed size (INITIAL_MEMORY). */
      heapSize: number;
    }
  | { id: number; ok: false; error: BounceErrorPayload };
