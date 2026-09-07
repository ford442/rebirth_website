#pragma once

#include <cstdint>
#include <vector>

namespace rb338 {

/**
 * Minimal RIFF/WAVE writer for 16-bit PCM.
 *
 * PCM needs no encoder library — this is a 44-byte header plus a clamped
 * float-to-int16 conversion, which is why nothing is vendored here (unlike
 * the read path, which uses dr_wav for the long tail of input variants).
 *
 * Samples are interleaved. Values outside [-1, 1] are clamped rather than
 * wrapped, so a hot mix distorts instead of exploding into noise.
 */
std::vector<uint8_t> writeWavPcm16(const float* interleaved, uint32_t frames,
                                   uint16_t channels, uint32_t sampleRate);

/** Byte length writeWavPcm16 will produce, for pre-sizing a buffer. */
uint32_t wavPcm16ByteSize(uint32_t frames, uint16_t channels);

/**
 * Incremental WAV encoder.
 *
 * Lets a long bounce be encoded block by block instead of buffering the whole
 * render as float first. That matters here: the shipping build has a fixed
 * 64 MiB heap with no growth, and a four-minute stereo song is ~81 MiB as
 * float32 but ~40 MiB as 16-bit PCM. Encoding as we go keeps only the output
 * file in memory.
 */
class WavPcm16Builder {
public:
  WavPcm16Builder(uint32_t expectedFrames, uint16_t channels, uint32_t sampleRate);

  /** Append `frames` worth of interleaved samples. */
  void append(const float* interleaved, uint32_t frames);

  /** Finish the file, patching the size fields, and hand over the bytes. */
  std::vector<uint8_t> take();

  uint32_t framesWritten() const { return m_framesWritten; }

private:
  std::vector<uint8_t> m_bytes;
  uint16_t m_channels;
  uint32_t m_framesWritten = 0;
};

} // namespace rb338
