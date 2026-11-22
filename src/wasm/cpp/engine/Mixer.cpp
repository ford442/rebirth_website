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
 * COMP byte 1 (`ratio`) — 0 is the gentlest squeeze the format can ask for,
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

constexpr float kLimiterThreshold = 0.92f;
constexpr float kLimiterRelease = 0.08f;      // seconds (attack is instant)

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

float Mixer::delaySeconds(uint8_t steps, bool triplet, float bpm) {
  // A step is a sixteenth, or a sixteenth triplet (two thirds of one).
  const float clampedBpm = std::clamp(bpm, MIN_BPM, MAX_BPM);
  const float sixteenth = (60.0f / clampedBpm) * 0.25f;
  const float clampedSteps =
      static_cast<float>(std::clamp<uint8_t>(steps, 1, MAX_DELAY_STEPS));
  return clampedSteps * sixteenth * (triplet ? (2.0f / 3.0f) : 1.0f);
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

void Mixer::compressStereo(float& left, float& right) {
  // Stereo-linked: one envelope on the louder side, one gain for both, so
  // the image does not wander under compression.
  const float level = std::max(std::fabs(left), std::fabs(right));
  const float coeff = (level > m_masterCompressorEnvelope) ? m_compressorAttackCoeff
                                                           : m_compressorReleaseCoeff;
  m_masterCompressorEnvelope = level + coeff * (m_masterCompressorEnvelope - level);
  const float env = m_masterCompressorEnvelope;
  if (env <= m_compressorThreshold || env <= 1e-8f) return;
  const float target = m_compressorThreshold + (env - m_compressorThreshold) / m_compressorRatio;
  const float gain = target / env;
  left *= gain;
  right *= gain;
}

void Mixer::resetDspState() {
  if (m_delayLine) std::fill_n(m_delayLine.get(), m_delayCapacity, 0.0f);
  m_delayWritePos = 0;
  m_delayTapSamples = 0;
  // Unity, not zero: the envelope is a gain, and starting it at 0 faded in
  // the first ~50 ms of every bounce and every snapshot rebuild.
  m_limiterEnvelope = 1.0f;
  m_compressorEnvelopes.fill(0.0f);
  m_masterCompressorEnvelope = 0.0f;
  for (auto& pcf : m_pcf) {
    pcf.reset();
  }
  for (auto& pcf : m_pcfMaster) {
    pcf.reset();
  }
  m_distShaper.reset();
  for (auto& line : m_distComp) line.fill(0.0f);
  m_distCompPos = 0;
}

void Mixer::init(float sampleRate) {
  m_sampleRate = std::max(sampleRate, 1000.0f);
  // One allocation, on the main thread. Sized so the slowest tempo-synced
  // subdivision fits at this sample rate; process() only ever indexes it.
  const size_t capacity = static_cast<size_t>(MAX_DELAY_SECONDS * m_sampleRate) + 1u;
  m_delayLine.reset(static_cast<float*>(std::malloc(capacity * sizeof(float))));
  m_delayCapacity = m_delayLine ? static_cast<uint32_t>(capacity) : 0u;
  refreshCompressorCoefficients();
  m_limiterReleaseCoeff = std::exp(-1.0f / (kLimiterRelease * m_sampleRate));
  for (auto& pcf : m_pcf) {
    pcf.prepare(m_sampleRate);
  }
  for (auto& pcf : m_pcfMaster) {
    pcf.prepare(m_sampleRate);
  }
  m_distShaper.prepare();
  resetDspState();

  for (auto& dev : m_devices) {
    dev.level = 0.8f;
    dev.pan = 0.5f;
    dev.muted = false;
    dev.dist = false;
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
  setDelaySteps(fx.delay.steps);
  m_delayTriplet = fx.delay.triplet;
  setDelayPan(std::min<uint8_t>(fx.delay.pan, 127) / 127.0f);
  m_distortionOn = fx.dist.enabled;
  m_distortionDrive = 1.0f + (fx.dist.drive / 127.0f) * 9.0f;
  m_distortionMix = std::clamp(fx.dist.mix / 127.0f, 0.0f, 1.0f);
  m_compressorOn = fx.comp.enabled;
  m_compressorThreshold = std::clamp(fx.comp.threshold / 127.0f, 0.0f, 1.0f);
  m_compressorRatio = compressorRatioFromByte(fx.comp.ratio);
  m_compressorAttackSeconds = compressorAttackFromByte(fx.comp.attack);
  refreshCompressorCoefficients();
  setCompressorTarget(fx.comp.target);
  m_pcfOn = fx.pcf.enabled;
  setPcfCutoff(std::clamp(fx.pcf.cutoff / 127.0f, 0.01f, 1.0f));
  setPcfResonance(std::clamp(fx.pcf.resonance / 127.0f, 0.0f, 1.0f));
  setPcfEnvAmount(std::clamp(fx.pcf.envAmount / 127.0f, 0.0f, 1.0f));
  setPcfDecay(std::min<uint8_t>(fx.pcf.decay, 127) / 127.0f);
  setPcfLowpass(fx.pcf.lowpass);
  setPcfTarget(fx.pcf.target);
}

void Mixer::setMasterLevel(float level) {
  m_masterLevel = std::clamp(level, 0.0f, 1.0f);
}

void Mixer::setDelaySteps(uint8_t steps) {
  m_delaySteps = std::clamp<uint8_t>(steps, 1, MAX_DELAY_STEPS);
}

void Mixer::setDelayTriplet(bool triplet) {
  m_delayTriplet = triplet;
}

void Mixer::setDelayFeedback(float feedback) {
  m_delayFeedback = std::clamp(feedback, 0.0f, 1.0f);
}

void Mixer::setDelayPan(float pan) {
  const float p = std::clamp(pan, 0.0f, 1.0f);
  m_delayPanL = constantPowerPanLeft(p);
  m_delayPanR = constantPowerPanRight(p);
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

void Mixer::setCompressorRatioByte(uint8_t ratio) {
  m_compressorRatio = compressorRatioFromByte(std::min<uint8_t>(ratio, 127));
}

void Mixer::setCompressorTarget(uint8_t target) {
  m_compressorTarget = target <= FX_TARGET_MAX ? target : FX_TARGET_NONE;
}

void Mixer::setPcfTarget(uint8_t target) {
  m_pcfTarget = target <= FX_TARGET_MAX ? target : FX_TARGET_NONE;
}

void Mixer::setPcfDecay(float decay) {
  for (auto& pcf : m_pcf) pcf.setDecay(decay);
  for (auto& pcf : m_pcfMaster) pcf.setDecay(decay);
}

void Mixer::setPcfLowpass(bool lowpass) {
  for (auto& pcf : m_pcf) pcf.setLowpass(lowpass);
  for (auto& pcf : m_pcfMaster) pcf.setLowpass(lowpass);
}

void Mixer::setCompressorAttack(float attackSeconds) {
  m_compressorAttackSeconds = std::clamp(attackSeconds, 0.0002f, 0.1f);
  refreshCompressorCoefficients();
}

void Mixer::setPcfCutoff(float cutoff) {
  m_pcfCutoff = std::clamp(cutoff, 0.01f, 1.0f);
  for (auto& pcf : m_pcf) pcf.setCutoff(m_pcfCutoff);
  for (auto& pcf : m_pcfMaster) pcf.setCutoff(m_pcfCutoff);
}

void Mixer::setPcfResonance(float resonance) {
  m_pcfResonance = std::clamp(resonance, 0.0f, 1.0f);
  for (auto& pcf : m_pcf) pcf.setResonance(m_pcfResonance);
  for (auto& pcf : m_pcfMaster) pcf.setResonance(m_pcfResonance);
}

void Mixer::setPcfEnvAmount(float envAmount) {
  m_pcfEnvAmount = std::clamp(envAmount, 0.0f, 1.0f);
  for (auto& pcf : m_pcf) pcf.setEnvAmount(m_pcfEnvAmount);
  for (auto& pcf : m_pcfMaster) pcf.setEnvAmount(m_pcfEnvAmount);
}

void Mixer::setChannelLevel(int deviceIndex, float level) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].level = std::clamp(level, 0.0f, 1.0f);
}

void Mixer::setChannelPan(int deviceIndex, float pan) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].pan = std::clamp(pan, 0.0f, 1.0f);
}

void Mixer::setChannelDelaySend(int deviceIndex, float send) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].delaySend = std::clamp(send, 0.0f, 1.0f);
}

void Mixer::setChannelDistortion(int deviceIndex, bool on) {
  if (deviceIndex < 0 || deviceIndex >= NUM_DEVICES) return;
  m_devices[static_cast<size_t>(deviceIndex)].dist = on;
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
  const float samplesPerTap = delaySeconds(m_delaySteps, m_delayTriplet, bpm) * m_sampleRate;
  const uint32_t tap = static_cast<uint32_t>(std::round(samplesPerTap));
  const uint32_t capacity = m_delayCapacity;
  m_delayTapSamples = (capacity < 2u) ? 0u : std::clamp(tap, 1u, capacity - 1u);
}

float Mixer::processDelaySample(float input) {
  if (!m_delayOn || m_delayTapSamples == 0 || !m_delayLine) {
    return 0.0f;
  }

  const uint32_t capacity = m_delayCapacity;
  float* line = m_delayLine.get();
  const uint32_t readPos = (m_delayWritePos + capacity - m_delayTapSamples) % capacity;
  const float delayed = line[readPos];

  line[m_delayWritePos] = flushDenormal(input + delayed * m_delayFeedback);
  m_delayWritePos = (m_delayWritePos + 1) % capacity;
  return delayed;
}

void Mixer::process(const float* const* deviceBuffers, float* leftOut, float* rightOut,
                    uint32_t numFrames, float bpm) {
  if (!leftOut || !rightOut || numFrames == 0) return;

  updateDelayTap(bpm);
  ::memset(leftOut, 0, numFrames * sizeof(float));
  ::memset(rightOut, 0, numFrames * sizeof(float));

  // Level and pan only change between process() calls (commands and
  // automation land on span boundaries), so the per-device gains are
  // computed once here — not a cos + sin per device per frame.
  std::array<float, NUM_DEVICES> gainL{};
  std::array<float, NUM_DEVICES> gainR{};
  std::array<float, NUM_DEVICES> levels{};
  std::array<float, NUM_DEVICES> sends{};
  std::array<bool, NUM_DEVICES> toDist{};
  std::array<bool, NUM_DEVICES> pcfHere{};
  std::array<bool, NUM_DEVICES> compHere{};
  bool anyDist = false;
  for (int d = 0; d < NUM_DEVICES; ++d) {
    const DeviceState& dev = m_devices[static_cast<size_t>(d)];
    const float pan = std::clamp(dev.pan, 0.0f, 1.0f);
    gainL[d] = constantPowerPanLeft(pan);
    gainR[d] = constantPowerPanRight(pan);
    levels[d] = std::clamp(dev.level, 0.0f, 1.0f);
    sends[d] = std::clamp(dev.delaySend, 0.0f, 1.0f);
    toDist[d] = m_distortionOn && dev.dist && !dev.muted;
    anyDist = anyDist || toDist[d];
    pcfHere[d] = m_pcfOn && insertOn(m_pcfTarget, d);
    compHere[d] = m_compressorOn && insertOn(m_compressorTarget, d);
  }
  const bool masterPcf = m_pcfOn && m_pcfTarget == FX_TARGET_MASTER;
  const bool masterComp = m_compressorOn && m_compressorTarget == FX_TARGET_MASTER;
  constexpr uint32_t kCompMask = kDistCompSize - 1u;
  const uint32_t latency = m_distOversampling ? dsp::OversampledShaper::kLatency : 0u;

  for (uint32_t frame = 0; frame < numFrames; ++frame) {
    float dryL = 0.0f;
    float dryR = 0.0f;
    float distBus = 0.0f;
    float delayBus = 0.0f;
    const uint32_t writePos = m_distCompPos;
    const uint32_t readPos = (writePos + kDistCompSize - latency) & kCompMask;

    for (int d = 0; d < NUM_DEVICES; ++d) {
      const float* src = deviceBuffers[d];
      if (!src || m_devices[d].muted) {
        m_distComp[d][writePos] = 0.0f;
        continue;
      }

      // Insert chain: PCF, then level, then compressor — each only on the
      // device MIXR picked for it.
      float sample = flushDenormal(src[frame]);
      if (pcfHere[d]) sample = m_pcf[static_cast<size_t>(d)].process(sample);
      sample *= levels[d];
      if (compHere[d]) sample = compressSample(sample, m_compressorEnvelopes[d]);

      // A channel feeding the distortion is heard dry kLatency samples
      // late, in step with its distorted copy.
      m_distComp[d][writePos] = sample;
      const float dry = toDist[d] ? m_distComp[d][readPos] : sample;
      dryL += dry * gainL[d];
      dryR += dry * gainR[d];

      if (toDist[d]) distBus += sample;
      if (m_delayOn && sends[d] > 0.0f) delayBus += sample * sends[d];
    }
    m_distCompPos = (writePos + 1u) & kCompMask;

    float wetL = 0.0f;
    float wetR = 0.0f;

    if (anyDist) {
      // Oversampled + ADAA (see OversampledShaper); the plain shaper is the
      // pre-oversampling behaviour, kept as a test reference.
      const float distorted = m_distOversampling
                                  ? m_distShaper.process(distBus, m_distortionDrive)
                                  : dsp::OversampledShaper::shape(distBus, m_distortionDrive);
      const float wet = distorted * m_distortionMix;
      wetL += wet;
      wetR += wet;
    }

    if (m_delayOn) {
      const float delayed = processDelaySample(delayBus);
      wetL += delayed * m_delayPanL;
      wetR += delayed * m_delayPanR;
    }

    float outL = dryL + wetL;
    float outR = dryR + wetR;
    if (masterPcf) {
      outL = m_pcfMaster[0].process(outL);
      outR = m_pcfMaster[1].process(outR);
    }
    if (masterComp) compressStereo(outL, outR);
    outL = flushDenormal(outL * m_masterLevel);
    outR = flushDenormal(outR * m_masterLevel);

    // Master peak limiter — always on, whatever the song's COMP switch says
    // (that switch is the compressor insert above). The gain drops
    // instantly to whatever keeps this sample under the ceiling, then
    // recovers smoothly. Attack and release used to share one 20 ms
    // coefficient, so a transient was half through before the gain moved and
    // the output clipped.
    const float peak = std::max(std::fabs(outL), std::fabs(outR));
    const float targetGain =
        (peak > kLimiterThreshold) ? (kLimiterThreshold / peak) : 1.0f;
    if (targetGain < m_limiterEnvelope) {
      m_limiterEnvelope = targetGain;
    } else {
      m_limiterEnvelope =
          targetGain + m_limiterReleaseCoeff * (m_limiterEnvelope - targetGain);
    }
    outL *= m_limiterEnvelope;
    outR *= m_limiterEnvelope;

    leftOut[frame] = flushDenormal(outL);
    rightOut[frame] = flushDenormal(outR);
  }
}

} // namespace rb338
