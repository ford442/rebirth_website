import { describe, expect, it } from 'vitest';
import { WAV_HEADER_BYTES, WavAssembler, writeWavHeader } from './wav-assembler';

describe('writeWavHeader', () => {
  it('matches the C++ writer layout for 16-bit stereo', () => {
    const out = new Uint8Array(WAV_HEADER_BYTES);
    writeWavHeader(out, 1000, 2, 44100);
    const view = new DataView(out.buffer);
    const tag = (at: number) => String.fromCharCode(...out.slice(at, at + 4));
    expect(tag(0)).toBe('RIFF');
    expect(view.getUint32(4, true)).toBe(36 + 4000);
    expect(tag(8)).toBe('WAVE');
    expect(tag(12)).toBe('fmt ');
    expect(view.getUint32(16, true)).toBe(16);
    expect(view.getUint16(20, true)).toBe(1);
    expect(view.getUint16(22, true)).toBe(2);
    expect(view.getUint32(24, true)).toBe(44100);
    expect(view.getUint32(28, true)).toBe(44100 * 4);
    expect(view.getUint16(32, true)).toBe(4);
    expect(view.getUint16(34, true)).toBe(16);
    expect(tag(36)).toBe('data');
    expect(view.getUint32(40, true)).toBe(4000);
  });

  it('refuses a buffer too small for the header', () => {
    expect(() => writeWavHeader(new Uint8Array(10), 1, 2, 44100)).toThrow(RangeError);
  });
});

describe('WavAssembler', () => {
  it('places chunks after the header in order and reports completion', () => {
    const wav = new WavAssembler(3, 48000);
    expect(wav.bytes.byteLength).toBe(WAV_HEADER_BYTES + 12);
    wav.append(Uint8Array.of(1, 2, 3, 4, 5, 6, 7, 8));
    expect(wav.complete).toBe(false);
    wav.append(Uint8Array.of(9, 10, 11, 12));
    expect(wav.complete).toBe(true);
    expect([...wav.bytes.slice(WAV_HEADER_BYTES)]).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]);
  });

  it('rejects a chunk that would overrun the file', () => {
    const wav = new WavAssembler(1, 44100);
    expect(() => wav.append(new Uint8Array(8))).toThrow(RangeError);
  });
});
