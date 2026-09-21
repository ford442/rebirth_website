#pragma once

#include "../parser/RbsTypes.h"
#include "../synth/dsp/Pcf.h"
#include <array>
#include <cstdint>
#include <vector>

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
  /**
   * Longest delay tap the line must be able to hold, in seconds.
   *
   * The slowest subdivision we sync to is a dotted eighth, and the tempo
   * floor is 40 BPM: (60/40) * 0.75 = 1.125 s. One extra sample of headroom
   * keeps the read pointer strictly behind the write pointer.
   *
   * The line used to be a fixed 20000-sample array (~0.45 s @ 44.1 kHz),
   * which silently truncated every tap longer than a sixteenth at slow
   * tempos and would have clipped outright at 48/96 kHz.
   */
  static constexpr float MAX_DELAY_SECONDS = 1.125f;

  Mixer() = default;

  /**
   * Initialise internal delay lines, filter states, etc.
   *
   * Sizes the delay line for `MAX_DELAY_SECONDS` at `sampleRate`. This is
   * the one allocation the mixer makes and it happens on the main thread;
   * `process()` never resizes, so the audio callback stays alloc-free.
   */
  void init(float sampleRate);

  /**
   * Clear time-varying DSP state — delay line, limiter and compressor
   * envelopes, PCF filter memory — while leaving levels, pans, mutes and FX
   * settings untouched.
   *
   * An offline bounce needs this so a second render starts from silence
   * rather than inheriting the previous render's delay tail; init() would
   * also reset the song's mixer configuration, which a bounce must keep.
   */
  void resetDspState();

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

  /** Current mute state, so a stem render can solo one device and restore. */
  bool channelMuted(int deviceIndex) const;

  void setDelayFeedback(float feedback);
  void setDelayWet(float wet);
  void setDistortionDrive(float drive);
  void setDistortionMix(float mix);
  void setCompressorThreshold(float threshold);
  void setCompressorRatio(float ratio);
  void setCompressorAttack(float attackSeconds);
  void setPcfCutoff(float cutoff);
  void setPcfResonance(float resonance);
  void setPcfEnvAmount(float envAmount);

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
  void refreshCompressorCoefficients();
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
  float m_compressorRatio = 4.0f;
  float m_compressorAttackSeconds = 0.003f;
  float m_compressorAttackCoeff = 0.0f;
  float m_compressorReleaseCoeff = 0.0f;
  float m_pcfCutoff = 0.5f;
  float m_pcfResonance = 0.2f;
  float m_pcfEnvAmount = 0.0f;
  /** Delay subdivision as a fraction of a beat (0.25 = a sixteenth). */
  float m_delayBeatFraction = 0.25f;

  // Tempo-sync delay (sized once in init, never resized in process).
  std::vector<float> m_delayLine;
  uint32_t m_delayWritePos = 0;
  uint32_t m_delayTapSamples = 0;
  float m_delayFeedback = 0.38f;
  float m_delayWet = 0.45f;

  // Master peak limiter envelope (stereo-linked).
  float m_limiterEnvelope = 0.0f;

  // Per-device compressor envelope followers (compressor-send path).
  std::array<float, NUM_DEVICES> m_compressorEnvelopes{};

  // Per-device PCF send filters.
  std::array<dsp::Pcf, NUM_DEVICES> m_pcf{};
};

} // namespace rb338
