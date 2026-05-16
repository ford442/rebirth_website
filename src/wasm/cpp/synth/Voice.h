#pragma once

#include "../parser/RbsTypes.h"
#include <cstdint>

namespace rb338 {

/**
 * Voice — abstract base class for all synthesis voices (303, 808, 909).
 *
 * Each voice renders one render quantum (128 frames) into a mono buffer.
 */
class Voice {
public:
  virtual ~Voice() = default;

  /** Initialise voice with host sample rate. */
  virtual void init(float sampleRate) = 0;

  /** Load pattern + device state data. Main thread only. */
  virtual void load(const DeviceState& state, const std::vector<Pattern>& patterns) = 0;

  /**
   * Render audio into `output`.
   *
   * @param output     Mono float buffer (at least `numFrames` elements)
   * @param numFrames  Number of frames to render
   */
  virtual void render(float* output, uint32_t numFrames) = 0;

  /** Trigger a step (called by Sequencer inside audio callback). */
  virtual void triggerStep(uint8_t stepIndex, const StepData& step) = 0;

  /** Set device state knobs (for real-time automation). */
  virtual void setParameter(const char* name, float value) = 0;

  /** Reset all state. */
  virtual void reset() = 0;
};

} // namespace rb338
