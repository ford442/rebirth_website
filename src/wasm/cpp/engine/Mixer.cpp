#include "Mixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb338 {

void Mixer::init(float sampleRate) {
  m_sampleRate = sampleRate;
  for (auto& dev : m_devices) {
    dev.level = 0.8f;
    dev.pan = 0.5f;
    dev.muted = false;
  }
}

void Mixer::setDeviceStates(const std::array<DeviceState, NUM_DEVICES>& devices) {
  m_devices = devices;
}

void Mixer::process(const float* const* deviceBuffers, float* stereoOut, uint32_t numFrames) {
  if (!stereoOut || numFrames == 0) return;

  std::memset(stereoOut, 0, numFrames * 2 * sizeof(float));

  for (int d = 0; d < NUM_DEVICES; ++d) {
    const float* src = deviceBuffers[d];
    if (!src) continue;
    if (m_devices[d].muted) continue;

    const float level = std::clamp(m_devices[d].level, 0.0f, 1.0f);
    const float pan = std::clamp(m_devices[d].pan, 0.0f, 1.0f);
    // Linear pan law: left gain = (1 - pan) * level, right gain = pan * level.
    const float leftGain = (1.0f - pan) * 2.0f * level;
    const float rightGain = pan * 2.0f * level;

    for (uint32_t i = 0; i < numFrames; ++i) {
      const float s = src[i];
      stereoOut[i * 2 + 0] += s * leftGain;
      stereoOut[i * 2 + 1] += s * rightGain;
    }
  }

  // Soft clip / cheap limiter to avoid hard digital clipping when many voices
  // hit simultaneously.
  for (uint32_t i = 0; i < numFrames * 2; ++i) {
    stereoOut[i] = std::tanh(stereoOut[i]);
  }
}

} // namespace rb338
