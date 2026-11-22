import type { EngineModule, RbsAudioEngineInstance } from '../types/wasm-audio-engine';
import { MASTER_BUS, OFFLINE_CHUNK_FRAMES, STEM_DEVICES } from '../types/wasm-audio-engine';
import { loadModIntoEngine, loadSongIntoEngine } from './wasm-engine-io';
import { locateWasmAsset } from './wasm-locate-file';
import type { BounceRequest, BounceResponse } from './bounce-protocol';

let modulePromise: Promise<EngineModule> | null = null;

async function instantiateModule(req: BounceRequest): Promise<EngineModule> {
  if (modulePromise) return modulePromise;
  modulePromise = (async () => {
    const glueModule = (await import(/* @vite-ignore */ req.glueScriptPath)) as {
      default: (opts: { locateFile: (path: string) => string }) => Promise<EngineModule>;
    };
    return glueModule.default({
      locateFile: (path: string) =>
        locateWasmAsset(path, {
          wasmPath: req.wasmPath,
          glueScriptPath: req.glueScriptPath,
          workletPath: req.workletPath,
          pthreadWorkerPath: req.pthreadWorkerPath,
        }),
    });
  })();
  return modulePromise;
}

function fillEngine(module: EngineModule, req: BounceRequest): RbsAudioEngineInstance {
  const engine = new module.RbsAudioEngine();
  engine.init(req.config);

  const songErr = loadSongIntoEngine(module, engine, new Uint8Array(req.songBytes));
  if (songErr) {
    engine.delete();
    throw songErr;
  }

  if (req.modBytes && req.modBytes.byteLength > 0) {
    const modErr = loadModIntoEngine(module, engine, new Uint8Array(req.modBytes));
    if (modErr) {
      engine.delete();
      throw modErr;
    }
  }

  for (const override of req.paramOverrides) {
    engine.setDeviceParam(override.deviceId, override.paramId, override.value);
  }
  engine.setTempo(req.tempo);
  return engine;
}

function postResponse(response: BounceResponse, transfer: Transferable[] = []) {
  (
    self as unknown as { postMessage: (msg: BounceResponse, t?: Transferable[]) => void }
  ).postMessage(response, transfer);
}

/**
 * Render one file in fixed chunks, posting each as a transferable buffer.
 * The WASM heap holds a single chunk at a time, so a 15-minute song costs
 * the same heap as a 15-second one.
 */
function streamFile(
  engine: RbsAudioEngineInstance,
  req: BounceRequest,
  file: number,
  deviceIndex: number,
  frames: number,
  sampleHeap: () => void
): void {
  if (!engine.beginOfflineRender(deviceIndex)) {
    throw { code: 'HEAP_EXHAUSTED', message: 'Not enough WASM heap to start the bounce.' };
  }
  try {
    postResponse({
      id: req.id,
      ok: true,
      kind: 'begin',
      file,
      frames,
      sampleRate: req.config.sampleRate,
    });
    let rendered = 0;
    while (rendered < frames) {
      const ask = Math.min(OFFLINE_CHUNK_FRAMES, frames - rendered);
      const chunk = engine.renderOfflineChunk(ask);
      const got = chunk.byteLength / 4;
      if (got === 0) {
        throw { code: 'PARSE_ERROR', message: 'The bounce stopped before the end of the song.' };
      }
      // A fresh JS-owned copy (see main.cpp), so its buffer can be handed
      // over rather than copied again.
      const data = chunk.buffer as ArrayBuffer;
      postResponse({ id: req.id, ok: true, kind: 'chunk', file, data }, [data]);
      rendered += got;
      sampleHeap();
    }
  } finally {
    engine.endOfflineRender();
  }
}

self.onmessage = (event: MessageEvent<BounceRequest>) => {
  const req = event.data;
  void (async () => {
    let engine: RbsAudioEngineInstance | null = null;
    try {
      const module = await instantiateModule(req);
      engine = fillEngine(module, req);
      const length = req.frames > 0 ? req.frames : engine.songLengthFrames();
      if (length === 0) {
        throw { code: 'PARSE_ERROR', message: 'Nothing to render.' };
      }

      const devices =
        req.kind === 'stems'
          ? STEM_DEVICES.map((device) => device.index)
          : [req.deviceIndex ?? MASTER_BUS];
      const heap = { peak: module.heapStats().usedBytes };
      devices.forEach((deviceIndex, file) => {
        streamFile(engine!, req, file, deviceIndex, length, () => {
          heap.peak = Math.max(heap.peak, module.heapStats().usedBytes);
        });
      });
      postResponse({
        id: req.id,
        ok: true,
        kind: 'done',
        files: devices.length,
        peakHeapBytes: heap.peak,
        heapSize: module.heapStats().heapSize,
      });
    } catch (err) {
      const error =
        err && typeof err === 'object' && 'code' in err && 'message' in err
          ? {
              code: String((err as { code: unknown }).code),
              message: String((err as { message: unknown }).message),
            }
          : {
              code: 'PARSE_ERROR',
              message: err instanceof Error ? err.message : 'Bounce failed.',
            };
      const response: BounceResponse = { id: req.id, ok: false, error };
      postResponse(response);
    } finally {
      engine?.delete();
    }
  })();
};
