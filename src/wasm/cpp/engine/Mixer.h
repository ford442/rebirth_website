#pragma once

#include "../parser/RbsTypes.h"
#include <array>
#include <cstdint>

namespace rb338 {

/**
 * Mixer — 4-channel stereo mixer + master FX bus.
 *
 * Processes one render quantum (128 frames) at a time.
 * Per-device level, pan, mute, and FX sends come from the loaded song's
 * DeviceState. Global FX modules honour EngineConfig feature flags.
 *
 * Master output level is applied by RbsAudioEngine after this mix (WASM-side).
 * The JS GainNode in WasmAudioBridge stays at unity — see audio-module.config.ts.
 */
class Mixer {
public:
  /** Maximum delay line length (~0.45 s @ 44.1 kHz). Allocated once in init(). */
  static constexpr size_t MAX_DELAY_SAMPLES = 20000;

  Mixer() = default;

  /** Initialise internal delay lines, filter states, etc. (no heap alloc). */
  void init(float sampleRate);

  /** Copy mixer routing from the loaded song (main thread). */
  void setDeviceStates(const std::array<DeviceState, NUM_DEVICES>& devices);

  /**
   * Mix device outputs into final stereo buffer.
   *
   * @param deviceBuffers  4 pointers to mono float buffers (one per device)
   * @param stereoOut      Interleaved stereo output [L,R,L,R,...]
   * @param numFrames      Number of frames to process
   * @param bpm            Effective tempo (BPM) for tempo-sync delay
   */
  void process(const float* const* deviceBuffers, float* stereoOut, uint32_t numFrames,
               float bpm);

  void setDistortionEnabled(bool on) { m_distortionOn = on; }
  void setCompressorEnabled(bool on) { m_compressorOn = on; }
  void setDelayEnabled(bool on) { m_delayOn = on; }

private:
  static float flushDenormal(float x);
  static float constantPowerPanLeft(float pan);
  static float constantPowerPanRight(float pan);
  static float diodeDistort(float x);
  float compressSample(float x, float& envelope) const;
  void updateDelayTap(float bpm);
  void processDelaySample(float input, float& outL, float& outR);

  float m_sampleRate = 44100.0f;
  bool m_distortionOn = true;
  bool m_compressorOn = true;
  bool m_delayOn = true;
  std::array<DeviceState, NUM_DEVICES> m_devices{};

  // Tempo-sync delay (fixed allocation in init, no heap in process).
  std::array<float, MAX_DELAY_SAMPLES> m_delayLine{};
  uint32_t m_delayWritePos = 0;
  uint32_t m_delayTapSamples = 0;
  float m_delayFeedback = 0.38f;
  float m_delayWet = 0.45f;

  // Master peak limiter envelope (stereo-linked).
  float m_limiterEnvelope = 0.0f;

  // Per-device compressor envelope followers (compressor-send path).
  std::array<float, NUM_DEVICES> m_compressorEnvelopes{};
};

} // namespace rb338
