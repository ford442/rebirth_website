#pragma once

#include <cstdint>

namespace rb338 {

/**
 * Mixer — 4-channel stereo mixer + master FX bus.
 *
 * Processes one render quantum (128 frames) at a time.
 * All processing is in-place on per-device float buffers.
 */
class Mixer {
public:
  Mixer() = default;

  /** Initialise internal delay lines, filter states, etc. */
  void init(float sampleRate);

  /**
   * Mix device outputs into final stereo buffer.
   *
   * @param deviceBuffers  4 pointers to mono float buffers (one per device)
   * @param stereoOut      Interleaved stereo output [L,R,L,R,...]
   * @param numFrames      Number of frames to process
   */
  void process(const float* const* deviceBuffers, float* stereoOut, uint32_t numFrames);

  /** Enable/disable master FX. */
  void setDistortionEnabled(bool on) { m_distortionOn = on; }
  void setCompressorEnabled(bool on) { m_compressorOn = on; }
  void setDelayEnabled(bool on) { m_delayOn = on; }

private:
  float m_sampleRate = 44100.0f;
  bool m_distortionOn = true;
  bool m_compressorOn = true;
  bool m_delayOn = true;

  // TODO: delay line, compressor state, distortion state
};

} // namespace rb338
