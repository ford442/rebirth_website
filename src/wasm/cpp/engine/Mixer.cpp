#include "Mixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rb338 {

namespace {

constexpr float kDenormalThreshold = 1e-15f;
// Distortion drive/mix and the compressor threshold became runtime-settable
// (song FX + TRAK automation); their defaults now live on the Mixer members
// in Mixer.h. The stale copies that used to sit here were unreferenced, and
// tripped -Wunused-const-variable under Clang, which broke the Emscripten
// build while GCC's native build stayed green.
// `ratio` and `attack` are now read from the song's COMP chunk (see
// Mixer::setSongFx); only the release stays fixed, because the format has no
// byte for it.
constexpr float kCompressorRelease = 0.08f;   // seconds

/**
 * DEVL/DELY byte 1 (`time`) selects a tempo-sync subdivision, expressed here
 * as a fraction of one beat. ReBirth's delay knob steps through musical
 * divisions rather than free milliseconds, and the byte was previously
 * parsed and then ignored — every song delayed by exactly one sixteenth.
 */
constexpr float kDelaySubdivisions[] = {
    0.25f,   // sixteenth
    0.375f,  // dotted sixteenth
    0.5f,    // eighth triplet feel / eighth
    0.5f,    // eighth
    0.75f,   // dotted eighth
    1.0f,    // quarter
};
constexpr size_t kNumDelaySubdivisions =
    sizeof(kDelaySubdivisions) / sizeof(kDelaySubdivisions[0]);

/**
 * COMP byte 2 (`ratio`) — 0 is the gentlest squeeze the format can ask for,
 * 127 the hardest. Mapped 1.5:1 … 12:1, which brackets the range the
 * original's compressor covers without ever becoming a brick-wall limiter
 * (the master limiter after the bus already does that job).
 */
float compressorRatioFromByte(uint8_t ratio) {
  return 1.5f + (static_cast<float>(ratio) / 127.0f) * 10.5f;
}

/**
 * COMP byte 3 (`attack`) — 0 is the fastest attack, 127 the slowest, mapped
 * 0.5 ms … 50 ms exponentially so the fast end has usable resolution.
 */
float compressorAttackFromByte(uint8_t attack) {
  const float norm = static_cast<float>(attack) / 127.0f;
  return 0.0005f * std::exp2(norm * 6.64f); // 0.5 ms → ~50 ms
}

float subdivisionForByte(uint8_t time) {
  const size_t index = static_cast<size_t>(time) * kNumDelaySubdivisions / 128u;
  return kDelaySubdivisions[std::min(index, kNumDelaySubdivisions - 1)];
}
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

float Mixer::diodeDistort(float x, float drive) {
  const float driven = x * drive;
  if (driven >= 0.0f) {
    return 1.0f - std::exp(-driven);
  }
  return -1.0f + std::exp(driven);
}

void Mixer::refreshCompressorCoefficients() {
  m_compressorAttackCoeff =
      std::exp(-1.0f / (std::max(m_compressorAttackSeconds, 1e-4f) * m_sampleRate));
  m_compressorReleaseCoeff = std::exp(-1.0f / (kCompressorRelease * m_sampleRate));
}

float Mixer::compressSample(float x, float& envelope) const {
  const float absX = std::fabs(x);
  const float coeff = (absX > envelope) ? m_compressorAttackCoeff : m_compressorReleaseCoeff;
  envelope = absX + coeff * (envelope - absX);

  if (envelope <= m_compressorThreshold) {
    return x;
  }

  const float over = envelope - m_compressorThreshold;
  const float gainReduction = m_compressorThreshold + over / m_compressorRatio;
  const float gain = (envelope > 1e-8f) ? gainReduction / envelope : 1.0f;
  return x * gain;
}

void Mixer::resetDspState() {
  std::fill(m_delayLine.begin(), m_delayLine.end(), 0.0f);
  m_delayWritePos = 0;
  m_delayTapSamples = 0;
  m_limiterEnvelope = 0.0f;
  m_compressorEnvelopes.fill(0.0f);
  for (auto& pcf : m_pcf) {
    pcf.reset();
  }
}

void Mixer::init(float sampleRate) {
  m_sampleRate = std::max(sampleRate, 1000.0f);
  // One allocation, on the main thread. Sized so the slowest tempo-synced
  // subdivision fits at this sample rate; process() only ever indexes it.
  m_delayLine.assign(
      static_cast<size_t>(MAX_DELAY_SECONDS * m_sampleRate) + 1u, 0.0f);
  refreshCompressorCoefficients();
  for (auto& pcf : m_pcf) {
    pcf.prepare(m_sampleRate);
  }
  resetDspState();

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

void Mixer::setSongFx(const SongFxSettings& fx) {
  m_masterLevel = std::clamp(fx.masterLevel / 127.0f, 0.0f, 1.0f);
  m_delayOn = fx.delay.enabled;
  m_delayFeedback = std::clamp(fx.delay.feedback / 127.0f, 0.0f, 1.0f);
  m_delayWet = std::clamp(fx.delay.wet / 127.0f, 0.0f, 1.0f);
  m_distortionOn = fx.dist.enabled;
  m_distortionDrive = 1.0f + (fx.dist.drive / 127.0f) * 9.0f;
  m_distortionMix = std::clamp(fx.dist.mix / 127.0f, 0.0f, 1.0f);
  m_compressorOn = fx.comp.enabled;
  m_compressorThreshold = std::clamp(fx.comp.threshold / 127.0f, 0.0f, 1.0f);
  m_compressorRatio = compressorRatioFromByte(fx.comp.ratio);
  m_compressorAttackSeconds = compressorAttackFromByte(fx.comp.attack);
  refreshCompressorCoefficients();
  m_delayBeatFraction = subdivisionForByte(fx.delay.time);
  m_pcfOn = fx.pcf.enabled;
  setPcfCutoff(std::clamp(fx.pcf.cutoff / 127.0f, 0.01f, 1.0f));
  setPcfResonance(std::clamp(fx.pcf.resonance / 127.0f, 0.0f, 1.0f));
  setPcfEnvAmount(std::clamp(fx.pcf.envAmount / 127.0f, 0.0f, 1.0f));
}

void Mixer::setDelayFeedback(float feedback) {
  m_delayFeedback = std::clamp(feedback, 0.0f, 1.0f);
}

void Mixer::setDelayWet(float wet) {
  m_delayWet = std::clamp(wet, 0.0f, 1.0f);
}

void Mixer::setDistortionDrive(float drive) {
  m_distortionDrive = std::clamp(1.0f + drive * 9.0f, 1.0f, 10.0f);
}

void Mixer::setDistortionMix(float mix) {
  m_distortionMix = std::clamp(mix, 0.0f, 1.0f);
}

void Mixer::setCompressorThreshold(float threshold) {
  m_compressorThreshold = std::clamp(threshold, 0.0f, 1.0f);
}

void Mixer::setCompressorRatio(float ratio) {
  m_compressorRatio = std::clamp(ratio, 1.0f, 20.0f);
}

void Mixer::setCompressorAttack(float attackSeconds) {
  m_compressorAttackSeconds = std::clamp(attackSeconds, 0.0002f, 0.1f);
  refreshCompressorCoefficients();
}

void Mixer::setPcfCutoff(float cutoff) {
  m_pcfCutoff = std::clamp(cutoff, 0.01f, 1.0f);
  for (auto& pcf : m_pcf) {
    pcf.setCutoff(m_pcfCutoff);
  }
}

void Mixer::setPcfResonance(float resonance) {
  m_pcfResonance = std::clamp(resonance, 0.0f, 1.0f);
  for (auto& pcf : m_pcf) {
    pcf.setResonance(m_pcfResonance);
  }
}

void Mixer::setPcfEnvAmount(float envAmount) {
  m_pcfEnvAmount = std::clamp(envAmount, 0.0f, 1.0f);
  for (auto& pcf : m_pcf) {
    pcf.setEnvAmount(m_pcfEnvAmount);
  }
}

float Mixer::processPcfSample(int deviceIndex, float input) {
  if (!m_pcfOn || !m_devices[static_cast<size_t>(deviceIndex)].pcf) {
    return input;
  }
  return m_pcf[static_cast<size_t>(deviceIndex)].process(input);
}

void Mixer::setChannelLevel(int deviceIndex, float level) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].level = std::clamp(level, 0.0f, 1.0f);
}

void Mixer::setChannelPan(int deviceIndex, float pan) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].pan = std::clamp(pan, 0.0f, 1.0f);
}

bool Mixer::channelMuted(int deviceIndex) const {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return false;
  return m_devices[static_cast<size_t>(deviceIndex)].muted;
}

void Mixer::setChannelMuted(int deviceIndex, bool muted) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].muted = muted;
}

void Mixer::updateDelayTap(float bpm) {
  const float clampedBpm = std::clamp(bpm, 40.0f, 250.0f);
  // Tempo-synced to the subdivision the song's DELY `time` byte selected.
  const float samplesPerTap = (60.0f / clampedBpm) * m_delayBeatFraction * m_sampleRate;
  const uint32_t tap = static_cast<uint32_t>(std::round(samplesPerTap));
  const uint32_t capacity = static_cast<uint32_t>(m_delayLine.size());
  m_delayTapSamples = (capacity < 2u) ? 0u : std::clamp(tap, 1u, capacity - 1u);
}

void Mixer::processDelaySample(float input, float& outL, float& outR) {
  if (!m_delayOn || m_delayTapSamples == 0 || m_delayLine.empty()) {
    outL = 0.0f;
    outR = 0.0f;
    return;
  }

  const uint32_t capacity = static_cast<uint32_t>(m_delayLine.size());
  const uint32_t readPos = (m_delayWritePos + capacity - m_delayTapSamples) % capacity;
  const float delayed = m_delayLine[readPos];

  m_delayLine[m_delayWritePos] =
      flushDenormal(input + delayed * m_delayFeedback);
  m_delayWritePos = (m_delayWritePos + 1) % capacity;

  const float wet = delayed * m_delayWet;
  outL = wet;
  outR = wet;
}

void Mixer::process(const float* const* deviceBuffers, float* leftOut, float* rightOut,
                    uint32_t numFrames, float bpm) {
  if (!leftOut || !rightOut || numFrames == 0) return;

  updateDelayTap(bpm);
  ::memset(leftOut, 0, numFrames * sizeof(float));
  ::memset(rightOut, 0, numFrames * sizeof(float));

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
      sample = processPcfSample(d, sample);
      const float level = std::clamp(m_devices[d].level, 0.0f, 1.0f);
      const float pan = std::clamp(m_devices[d].pan, 0.0f, 1.0f);

      if (m_compressorOn && m_devices[d].compressor) {
        // Per-device compressor send — gentle bus compression before pan.
        sample = compressSample(sample * level, m_compressorEnvelopes[d]);
      } else {
        sample *= level;
      }

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
    }

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (m_distortionOn && std::fabs(distBus) > kDenormalThreshold) {
      const float distorted = diodeDistort(distBus, m_distortionDrive);
      const float wet = distorted * m_distortionMix;
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

    float outL = flushDenormal((dryL + wetL) * m_masterLevel);
    float outR = flushDenormal((dryR + wetR) * m_masterLevel);

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
