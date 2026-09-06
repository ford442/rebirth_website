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
  void setSongFx(const SongFxSettings& fx);

  /**
   * Mix device outputs into planar stereo buffers.
   *
   * @param deviceBuffers  4 pointers to mono float buffers (one per device)
   * @param leftOut        Left channel output (numFrames samples)
   * @param rightOut       Right channel output (numFrames samples)
   * @param numFrames      Number of frames to process
   * @param bpm            Effective tempo (BPM) for tempo-sync delay
   */
  void process(const float* const* deviceBuffers, float* leftOut, float* rightOut,
               uint32_t numFrames, float bpm);

  void setChannelLevel(int deviceIndex, float level);
  void setChannelPan(int deviceIndex, float pan);
  void setChannelMuted(int deviceIndex, bool muted);

  void setDelayFeedback(float feedback);
  void setDelayWet(float wet);
  void setDistortionDrive(float drive);
  void setDistortionMix(float mix);
  void setCompressorThreshold(float threshold);
  void setPcfCutoff(float cutoff);
  void setPcfResonance(float resonance);

  void setDistortionEnabled(bool on) { m_distortionOn = on; }
  void setCompressorEnabled(bool on) { m_compressorOn = on; }
  void setDelayEnabled(bool on) { m_delayOn = on; }

private:
  static float flushDenormal(float x);
  static float constantPowerPanLeft(float pan);
  static float constantPowerPanRight(float pan);
  static float diodeDistort(float x, float drive);
  float compressSample(float x, float& envelope) const;
  void updateDelayTap(float bpm);
  void processDelaySample(float input, float& outL, float& outR);
  float processPcfSample(int deviceIndex, float input);

  float m_sampleRate = 44100.0f;
  bool m_distortionOn = true;
  bool m_compressorOn = true;
  bool m_delayOn = true;
  bool m_pcfOn = false;
  std::array<DeviceState, NUM_DEVICES> m_devices{};

  float m_masterLevel = 1.0f;
  float m_distortionDrive = 5.0f;
  float m_distortionMix = 0.65f;
  float m_compressorThreshold = 0.55f;
  float m_pcfCutoff = 0.5f;
  float m_pcfResonance = 0.2f;

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
  std::array<float, NUM_DEVICES> m_pcfState{};
};

} // namespace rb338
