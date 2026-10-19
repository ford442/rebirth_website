/**
 * Copy song/mod bytes onto the WASM heap and drive Embind load helpers.
 * Shared by the live bridge and the bounce Worker so malloc(0) is never written.
 */

import type { EngineError, EngineModule, RbsAudioEngineInstance } from '../types/wasm-audio-engine';
import { MOD_LOAD_STATUSES, type ParsedMod } from '../types/wasm-audio-mod';
import { toUiParsedMod, toUiParsedSong } from '../types/wasm-audio-mapping';

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

/** Human-readable copy for a failed mod load, for the LCD / toast. */
export function modStatusMessage(status: string): string {
  switch (status) {
    case 'arena-exhausted':
      return 'Mod is too large for the sample memory budget.';
    case 'no-samples':
      return 'No playable samples in this mod (it may be skins only).';
    case 'not-initialised':
      return 'Audio engine is not ready yet.';
    default:
      return 'Mod could not be loaded.';
  }
}

/**
 * The error a finished mod load amounts to, or null when it is usable.
 *
 * `loadMod` returns a status instead of throwing, so something has to read
 * it. A mod that supplies at least one sample is usable (slots it leaves out
 * keep their procedural voices); one that supplies none is a failure, and
 * the live bridge and the bounce Worker must agree on that — otherwise a
 * bounce quietly renders procedural drums for a kit you can hear playing.
 */
export function modLoadError(status: number, report: ParsedMod): EngineError | null {
  if (report.loadedSlots > 0) return null;
  return {
    code: 'MOD_LOAD_ERROR',
    message: modStatusMessage(MOD_LOAD_STATUSES[status] ?? 'no-samples'),
  };
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
    const status = engine.loadMod(ptr, modBytes.byteLength);
    return modLoadError(status, toUiParsedMod(engine.getModReport()));
  } finally {
    module._free(ptr);
  }
}
