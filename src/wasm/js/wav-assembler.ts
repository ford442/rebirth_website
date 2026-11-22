/**
 * WAV assembly for the chunked offline bounce.
 *
 * The bounce worker streams interleaved 16-bit stereo PCM out of the WASM
 * heap one fixed-size chunk at a time; this writes the RIFF header and copies
 * each chunk into place, so the file is only ever whole on the JavaScript
 * side. The header layout mirrors `writeWavPcm16` in
 * `src/wasm/cpp/audio/WavWriter.cpp` byte for byte.
 */

/** Size of a canonical PCM `RIFF`/`WAVE` header with one `fmt ` and one `data` chunk. */
export const WAV_HEADER_BYTES = 44;

const BYTES_PER_SAMPLE = 2;

/** Write the 44-byte header for `frames` of 16-bit PCM at the start of `out`. */
export function writeWavHeader(
  out: Uint8Array,
  frames: number,
  channels: number,
  sampleRate: number
): void {
  if (out.byteLength < WAV_HEADER_BYTES) {
    throw new RangeError('WAV header needs 44 bytes');
  }
  const view = new DataView(out.buffer, out.byteOffset, WAV_HEADER_BYTES);
  const dataBytes = frames * channels * BYTES_PER_SAMPLE;
  const ascii = (offset: number, id: string) => {
    for (let i = 0; i < 4; i += 1) view.setUint8(offset + i, id.charCodeAt(i));
  };
  ascii(0, 'RIFF');
  view.setUint32(4, 36 + dataBytes, true);
  ascii(8, 'WAVE');
  ascii(12, 'fmt ');
  view.setUint32(16, 16, true); // PCM fmt chunk size
  view.setUint16(20, 1, true); // format tag: PCM
  view.setUint16(22, channels, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * channels * BYTES_PER_SAMPLE, true);
  view.setUint16(32, channels * BYTES_PER_SAMPLE, true);
  view.setUint16(34, 16, true); // bits per sample
  ascii(36, 'data');
  view.setUint32(40, dataBytes, true);
}

/**
 * One WAV file being filled chunk by chunk. The buffer is allocated once at
 * its final size, so assembling costs the file plus nothing.
 */
export class WavAssembler {
  readonly bytes: Uint8Array;
  private offset = WAV_HEADER_BYTES;

  constructor(
    readonly frames: number,
    readonly sampleRate: number,
    readonly channels = 2
  ) {
    this.bytes = new Uint8Array(WAV_HEADER_BYTES + frames * channels * BYTES_PER_SAMPLE);
    writeWavHeader(this.bytes, frames, channels, sampleRate);
  }

  /** Append one chunk of interleaved PCM16 bytes. */
  append(chunk: Uint8Array): void {
    if (this.offset + chunk.byteLength > this.bytes.byteLength) {
      throw new RangeError('Bounce chunk overruns the WAV it belongs to');
    }
    this.bytes.set(chunk, this.offset);
    this.offset += chunk.byteLength;
  }

  /** True once every frame the header promises has arrived. */
  get complete(): boolean {
    return this.offset === this.bytes.byteLength;
  }
}
