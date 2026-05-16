#include "Mixer.h"
#include <cstring>

namespace rb338 {

void Mixer::init(float sampleRate) {
  m_sampleRate = sampleRate;
  // TODO: allocate delay line, init filter states
}

void Mixer::process(const float* const* deviceBuffers, float* stereoOut, uint32_t numFrames) {
  // TODO: sum device buffers, apply pan, level, master FX
  // For now, just copy first device to stereo (placeholder)
  if (deviceBuffers[0]) {
    for (uint32_t i = 0; i < numFrames; ++i) {
      float s = deviceBuffers[0][i];
      stereoOut[i * 2 + 0] = s;
      stereoOut[i * 2 + 1] = s;
    }
  } else {
    std::memset(stereoOut, 0, numFrames * 2 * sizeof(float));
  }
}

} // namespace rb338
