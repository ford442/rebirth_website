import type { EngineModule, RbsAudioEngineInstance } from '../types/wasm-audio-engine';
import { MASTER_BUS, STEM_DEVICES } from '../types/wasm-audio-engine';
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

function wavBuffer(bytes: Uint8Array): ArrayBuffer {
  const copy = new Uint8Array(bytes.byteLength);
  copy.set(bytes);
  return copy.buffer;
}

function postResponse(response: BounceResponse, transfer: Transferable[] = []) {
  (
    self as unknown as { postMessage: (msg: BounceResponse, t?: Transferable[]) => void }
  ).postMessage(response, transfer);
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

      if (req.kind === 'stems') {
        const wavs: ArrayBuffer[] = [];
        for (const device of STEM_DEVICES) {
          const bytes = engine.renderOfflineToWav(length, device.index);
          wavs.push(wavBuffer(bytes));
        }
        const response: BounceResponse = { id: req.id, ok: true, kind: 'stems', wavs };
        postResponse(response, wavs);
        return;
      }

      const bytes = engine.renderOfflineToWav(length, req.deviceIndex ?? MASTER_BUS);
      const wav = wavBuffer(bytes);
      const response: BounceResponse = { id: req.id, ok: true, kind: 'wav', wav };
      postResponse(response, [wav]);
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
