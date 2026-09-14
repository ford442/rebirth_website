export interface BounceParamOverride {
  deviceId: number;
  paramId: number;
  value: number;
}

export interface BounceEngineFeatures {
  sampleRate: number;
  bufferSize: number;
  enableTb303A: boolean;
  enableTb303B: boolean;
  enableTr808: boolean;
  enableTr909: boolean;
  enableDistortion: boolean;
  enableCompressor: boolean;
  enableDelay: boolean;
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

export type BounceResponse =
  | { id: number; ok: true; kind: 'wav'; wav: ArrayBuffer }
  | { id: number; ok: true; kind: 'stems'; wavs: ArrayBuffer[] }
  | { id: number; ok: false; error: BounceErrorPayload };
