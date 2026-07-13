#include "Mixer.h"
#include "../parser/RbsTypes.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb338 {

void Mixer::init(float sampleRate) {
  m_sampleRate = sampleRate;
  // TODO: allocate delay line and init FX state when distortion/compressor/delay
  // are implemented. For now the mixer is a simple sum + pan.
}

void Mixer::process(const float* const* deviceBuffers, float* stereoOut, uint32_t numFrames) {
  if (!stereoOut || numFrames == 0) return;

  // Default per-device levels/pans until voice state carries them through.
  // Order: 303-A, 303-B, 808, 909.
  const float levels[NUM_DEVICES] = { 0.7f, 0.7f, 0.8f, 0.8f };
  const float pans[NUM_DEVICES]   = { 0.4f, 0.6f, 0.5f, 0.5f }; // 0=left, 0.5=center, 1=right

  std::memset(stereoOut, 0, numFrames * 2 * sizeof(float));

  for (int d = 0; d < NUM_DEVICES; ++d) {
    const float* src = deviceBuffers[d];
    if (!src) continue;

    const float level = levels[d];
    // Linear pan law: left gain = (1 - pan) * level, right gain = pan * level.
    const float leftGain = (1.0f - pans[d]) * 2.0f * level;
    const float rightGain = pans[d] * 2.0f * level;

    for (uint32_t i = 0; i < numFrames; ++i) {
      const float s = src[i];
      stereoOut[i * 2 + 0] += s * leftGain;
      stereoOut[i * 2 + 1] += s * rightGain;
    }
  }

  // Soft clip / cheap limiter to avoid hard digital clipping when many voices
  // hit simultaneously.
  for (uint32_t i = 0; i < numFrames * 2; ++i) {
    float s = stereoOut[i];
    // tanh saturation
    s = std::tanh(s);
    stereoOut[i] = s;
  }
}

} // namespace rb338
