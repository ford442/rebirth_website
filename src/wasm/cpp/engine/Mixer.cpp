#include "Mixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

namespace rb338 {

namespace {

constexpr float kDenormalThreshold = 1e-15f;
constexpr float kDistortionDrive = 5.0f;
constexpr float kDistortionMix = 0.65f;
constexpr float kCompressorThreshold = 0.55f;
constexpr float kCompressorRatio = 4.0f;
constexpr float kCompressorAttack = 0.003f;   // seconds
constexpr float kCompressorRelease = 0.08f;   // seconds
constexpr float kLimiterThreshold = 0.92f;
constexpr float kLimiterRelease = 0.02f;      // seconds

} // anonymous namespace

float Mixer::flushDenormal(float x) {
  return (std::fabs(x) < kDenormalThreshold) ? 0.0f : x;
}

float Mixer::constantPowerPanLeft(float pan) {
  // pan 0 = hard left, 0.5 = centre, 1 = hard right
  const float angle = pan * 1.57079632679f; // pi/2
  return std::cos(angle);
}

float Mixer::constantPowerPanRight(float pan) {
  const float angle = pan * 1.57079632679f;
  return std::sin(angle);
}

float Mixer::diodeDistort(float x) {
  // Asymmetric soft clip — ReBirth-style diode distortion character.
  const float driven = x * kDistortionDrive;
  if (driven >= 0.0f) {
    return 1.0f - std::exp(-driven);
  }
  return -1.0f + std::exp(driven);
}

float Mixer::compressSample(float x, float& envelope) const {
  const float absX = std::fabs(x);
  const float attackCoeff = std::exp(-1.0f / (kCompressorAttack * m_sampleRate));
  const float releaseCoeff = std::exp(-1.0f / (kCompressorRelease * m_sampleRate));
  const float coeff = (absX > envelope) ? attackCoeff : releaseCoeff;
  envelope = absX + coeff * (envelope - absX);

  if (envelope <= kCompressorThreshold) {
    return x;
  }

  const float over = envelope - kCompressorThreshold;
  const float gainReduction = kCompressorThreshold + over / kCompressorRatio;
  const float gain = (envelope > 1e-8f) ? gainReduction / envelope : 1.0f;
  return x * gain;
}

void Mixer::init(float sampleRate) {
  m_sampleRate = std::max(sampleRate, 1000.0f);
  m_delayLine.fill(0.0f);
  m_delayWritePos = 0;
  m_delayTapSamples = 0;
  m_limiterEnvelope = 0.0f;
  m_compressorEnvelopes.fill(0.0f);

  for (auto& dev : m_devices) {
    dev.level = 0.8f;
    dev.pan = 0.5f;
    dev.muted = false;
    dev.dist = false;
    dev.compressor = false;
    dev.delaySend = 0.0f;
  }
}

void Mixer::setDeviceStates(const std::array<DeviceState, NUM_DEVICES>& devices) {
  m_devices = devices;
}

void Mixer::setChannelLevel(int deviceIndex, float level) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].level = std::clamp(level, 0.0f, 1.0f);
}

void Mixer::setChannelPan(int deviceIndex, float pan) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].pan = std::clamp(pan, 0.0f, 1.0f);
}

void Mixer::setChannelMuted(int deviceIndex, bool muted) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].muted = muted;
}

void Mixer::updateDelayTap(float bpm) {
  const float clampedBpm = std::clamp(bpm, 40.0f, 250.0f);
  // One 16th-note step (ReBirth master step) — tempo-sync delay subdivision.
  const float samplesPerStep = (60.0f / clampedBpm) * 0.25f * m_sampleRate;
  const uint32_t tap = static_cast<uint32_t>(std::round(samplesPerStep));
  m_delayTapSamples = std::clamp(tap, 1u, static_cast<uint32_t>(MAX_DELAY_SAMPLES - 1));
}

void Mixer::processDelaySample(float input, float& outL, float& outR) {
  if (!m_delayOn || m_delayTapSamples == 0) {
    outL = 0.0f;
    outR = 0.0f;
    return;
  }

  const uint32_t readPos =
      (m_delayWritePos + MAX_DELAY_SAMPLES - m_delayTapSamples) % MAX_DELAY_SAMPLES;
  const float delayed = m_delayLine[readPos];

  m_delayLine[m_delayWritePos] =
      flushDenormal(input + delayed * m_delayFeedback);
  m_delayWritePos = (m_delayWritePos + 1) % MAX_DELAY_SAMPLES;

  const float wet = delayed * m_delayWet;
  outL = wet;
  outR = wet;
}

void Mixer::process(const float* const* deviceBuffers, float* leftOut, float* rightOut,
                    uint32_t numFrames, float bpm) {
  if (!leftOut || !rightOut || numFrames == 0) return;

  updateDelayTap(bpm);
  std::memset(leftOut, 0, numFrames * sizeof(float));
  std::memset(rightOut, 0, numFrames * sizeof(float));

  const float limiterReleaseCoeff =
      std::exp(-1.0f / (kLimiterRelease * m_sampleRate));

  for (uint32_t frame = 0; frame < numFrames; ++frame) {
    float dryL = 0.0f;
    float dryR = 0.0f;
    float distBus = 0.0f;
    float delayBus = 0.0f;

    for (int d = 0; d < NUM_DEVICES; ++d) {
      const float* src = deviceBuffers[d];
      if (!src) continue;
      if (m_devices[d].muted) continue;

      float sample = flushDenormal(src[frame]);
      const float level = std::clamp(m_devices[d].level, 0.0f, 1.0f);
      const float pan = std::clamp(m_devices[d].pan, 0.0f, 1.0f);

      if (m_compressorOn && m_devices[d].compressor) {
        // Per-device compressor send — gentle bus compression before pan.
        sample = compressSample(sample * level, m_compressorEnvelopes[d]);
      } else {
        sample *= level;
      }

#ifdef __wasm_simd128__
      const v128_t gains = wasm_f32x4_make(
          constantPowerPanLeft(pan), constantPowerPanRight(pan),
          (m_distortionOn && m_devices[d].dist) ? 1.0f : 0.0f,
          m_delayOn ? std::clamp(m_devices[d].delaySend, 0.0f, 1.0f) : 0.0f);
      const v128_t scaled = wasm_f32x4_mul(wasm_f32x4_splat(sample), gains);
      alignas(16) float lanes[4];
      wasm_v128_store(lanes, scaled);
      dryL += lanes[0];
      dryR += lanes[1];
      distBus += lanes[2];
      delayBus += lanes[3];
#else
      const float leftGain = constantPowerPanLeft(pan);
      const float rightGain = constantPowerPanRight(pan);
      dryL += sample * leftGain;
      dryR += sample * rightGain;

      if (m_distortionOn && m_devices[d].dist) {
        distBus += sample;
      }

      if (m_delayOn) {
        const float send = std::clamp(m_devices[d].delaySend, 0.0f, 1.0f);
        if (send > 0.0f) {
          delayBus += sample * send;
        }
      }
#endif
    }

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (m_distortionOn && std::fabs(distBus) > kDenormalThreshold) {
      const float distorted = diodeDistort(distBus);
      const float wet = distorted * kDistortionMix;
      wetL += wet;
      wetR += wet;
    }

    if (m_delayOn) {
      float delayL = 0.0f;
      float delayR = 0.0f;
      processDelaySample(delayBus, delayL, delayR);
      wetL += delayL;
      wetR += delayR;
    }

    float outL = flushDenormal(dryL + wetL);
    float outR = flushDenormal(dryR + wetR);

    if (m_compressorOn) {
      // Master peak limiter — prevents hard digital clips on the summed bus.
      const float peak = std::max(std::fabs(outL), std::fabs(outR));
      const float targetGain =
          (peak > kLimiterThreshold) ? (kLimiterThreshold / peak) : 1.0f;
      m_limiterEnvelope =
          targetGain + limiterReleaseCoeff * (m_limiterEnvelope - targetGain);
      outL *= m_limiterEnvelope;
      outR *= m_limiterEnvelope;
    }

    leftOut[frame] = flushDenormal(outL);
    rightOut[frame] = flushDenormal(outR);
  }
}

} // namespace rb338
