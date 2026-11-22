#pragma once

#include "../parser/RbsTypes.h"
#include "../synth/dsp/OversampledShaper.h"
#include "../synth/dsp/Pcf.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>

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
  /** DELY byte 1: the delay time in steps, 1–32. */
  static constexpr uint8_t MAX_DELAY_STEPS = 32;

  /**
   * Delay-line length, in seconds. 32 straight sixteenths (two bars) at
   * MIN_BPM would be 24 s — 4.6 MB a mixer at 48 kHz, against a fixed
   * 64 MiB heap that holds two or three of them — so the line covers 8 s
   * (the longest step count seen in the archive corpus is 12) and a longer
   * tap is clamped. One extra sample keeps the read pointer strictly behind
   * the write pointer.
   */
  static constexpr float MAX_DELAY_SECONDS = 8.0f;

  /** Delay time in seconds for `steps` sixteenths (or triplet sixteenths). */
  static float delaySeconds(uint8_t steps, bool triplet, float bpm);

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
  void setChannelDelaySend(int deviceIndex, float send);
  void setChannelDistortion(int deviceIndex, bool on);

  /** Current mute state, so a stem render can solo one device and restore. */
  bool channelMuted(int deviceIndex) const;

  /** MIXR byte 0 as 0–1. */
  void setMasterLevel(float level);

  void setDelaySteps(uint8_t steps);
  void setDelayTriplet(bool triplet);
  void setDelayFeedback(float feedback);
  /** Delay return pan, 0 = left, 0.5 = centre, 1 = right. */
  void setDelayPan(float pan);
  void setDistortionDrive(float drive);
  void setDistortionMix(float mix);
  void setCompressorThreshold(float threshold);
  void setCompressorRatio(float ratio);
  /** COMP byte 1 (0–127) mapped as the song's chunk is. */
  void setCompressorRatioByte(uint8_t ratio);
  void setCompressorAttack(float attackSeconds);
  /** Insert device id, as MIXR byte 1 stores it (FX_TARGET_*). */
  void setCompressorTarget(uint8_t target);
  void setPcfCutoff(float cutoff);
  void setPcfResonance(float resonance);
  void setPcfEnvAmount(float envAmount);
  void setPcfDecay(float decay);
  void setPcfLowpass(bool lowpass);
  /** Insert device id, as MIXR byte 2 stores it (FX_TARGET_*). */
  void setPcfTarget(uint8_t target);

  void setDistortionEnabled(bool on) { m_distortionOn = on; }
  void setCompressorEnabled(bool on) { m_compressorOn = on; }
  void setDelayEnabled(bool on) { m_delayOn = on; }
  void setPcfEnabled(bool on) { m_pcfOn = on; }

  /** Test hook: run the distortion's shaper at the host rate, as it used to. */
  void setDistortionOversampling(bool on) { m_distOversampling = on; }

private:
  static float flushDenormal(float x);
  static float constantPowerPanLeft(float pan);
  static float constantPowerPanRight(float pan);
  float compressSample(float x, float& envelope) const;
  /** Stereo-linked compression of the master bus. */
  void compressStereo(float& left, float& right);
  void updateDelayTap(float bpm);
  void refreshCompressorCoefficients();
  float processDelaySample(float input);
  bool insertOn(uint8_t target, int deviceIndex) const {
    return target == FX_TARGET_FIRST_DEVICE + deviceIndex;
  }

  float m_sampleRate = 44100.0f;
  bool m_distortionOn = true;
  bool m_compressorOn = true;
  bool m_delayOn = true;
  bool m_pcfOn = false;
  std::array<DeviceState, NUM_DEVICES> m_devices{};

  float m_masterLevel = 1.0f;
  float m_distortionDrive = 5.0f;
  float m_distortionMix = 0.65f;
  bool m_distOversampling = true;
  dsp::OversampledShaper m_distShaper;
  /**
   * Per-channel delay matching the shaper's latency: a channel feeding the
   * (parallel) distortion has its dry signal delayed by as much, or the two
   * would comb-filter. Power of two so the index wraps with a mask.
   */
  static constexpr uint32_t kDistCompSize = 32;
  static_assert(dsp::OversampledShaper::kLatency < kDistCompSize);
  std::array<std::array<float, kDistCompSize>, NUM_DEVICES> m_distComp{};
  uint32_t m_distCompPos = 0;
  uint8_t m_compressorTarget = FX_TARGET_NONE;
  uint8_t m_pcfTarget = FX_TARGET_NONE;
  float m_compressorThreshold = 0.55f;
  float m_compressorRatio = 4.0f;
  float m_compressorAttackSeconds = 0.003f;
  float m_compressorAttackCoeff = 0.0f;
  float m_compressorReleaseCoeff = 0.0f;
  float m_pcfCutoff = 0.5f;
  float m_pcfResonance = 0.2f;
  float m_pcfEnvAmount = 0.0f;
  uint8_t m_delaySteps = 3;
  bool m_delayTriplet = false;
  float m_delayPanL = 0.70710678f;
  float m_delayPanR = 0.70710678f;

  // Tempo-sync delay (sized once in init, never resized in process).
  // malloc'd and null-checked: if the fixed heap cannot spare it the mixer
  // runs without the delay rather than aborting the WASM instance.
  struct FreeDeleter {
    void operator()(float* p) const { std::free(p); }
  };
  std::unique_ptr<float, FreeDeleter> m_delayLine;
  uint32_t m_delayCapacity = 0;
  uint32_t m_delayWritePos = 0;
  uint32_t m_delayTapSamples = 0;
  float m_delayFeedback = 0.38f;

  // Master peak limiter gain (stereo-linked, 1 = no reduction).
  float m_limiterEnvelope = 1.0f;
  float m_limiterReleaseCoeff = 0.0f;

  // Compressor envelope followers: one per device insert, one for the master.
  std::array<float, NUM_DEVICES> m_compressorEnvelopes{};
  float m_masterCompressorEnvelope = 0.0f;

  // PCF filters: one per device insert, a stereo pair for the master.
  std::array<dsp::Pcf, NUM_DEVICES> m_pcf{};
  std::array<dsp::Pcf, 2> m_pcfMaster{};
};

} // namespace rb338
