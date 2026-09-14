/**
 * Copy song/mod bytes onto the WASM heap and drive Embind load helpers.
 * Shared by the live bridge and the bounce Worker so malloc(0) is never written.
 */

import type { EngineError, EngineModule, RbsAudioEngineInstance } from '../types/wasm-audio';
import { toUiParsedSong } from '../types/wasm-audio-mapping';

export function mallocCopy(module: EngineModule, bytes: Uint8Array): number {
  const ptr = module._malloc(bytes.byteLength);
  if (ptr === 0) return 0;
  module.HEAPU8.set(bytes, ptr);
  return ptr;
}

export function loadSongIntoEngine(
  module: EngineModule,
  engine: RbsAudioEngineInstance,
  songBytes: Uint8Array
): EngineError | null {
  const ptr = mallocCopy(module, songBytes);
  if (ptr === 0) {
    return {
      code: 'PARSE_ERROR',
      message: `Not enough WASM heap for a ${Math.round(songBytes.byteLength / 1024)} KB song.`,
    };
  }
  try {
    const parsed = engine.loadSongFromBytes(ptr, songBytes.byteLength);
    if (parsed === undefined) {
      return {
        code: 'PARSE_ERROR',
        message: engine.lastParseError() || 'Song parse failed.',
      };
    }
    toUiParsedSong(parsed);
    return null;
  } finally {
    module._free(ptr);
  }
}

export function loadModIntoEngine(
  module: EngineModule,
  engine: RbsAudioEngineInstance,
  modBytes: Uint8Array
): EngineError | null {
  const ptr = mallocCopy(module, modBytes);
  if (ptr === 0) {
    return {
      code: 'MOD_TOO_LARGE',
      message: `Not enough WASM heap for a ${Math.round(modBytes.byteLength / 1024)} KB mod.`,
    };
  }
  try {
    engine.loadMod(ptr, modBytes.byteLength);
    return null;
  } finally {
    module._free(ptr);
  }
}
