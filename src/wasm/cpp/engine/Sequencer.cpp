#include "Sequencer.h"

namespace rb338 {

void Sequencer::load(const ParsedSong& song) {
  // TODO: build internal pattern lookup table
  m_sampleCounter = 0;
  m_currentBar = 1;
  m_currentStep = 0;
}

void Sequencer::generateEvents(float bpm, float sampleRate, uint32_t numFrames, std::vector<Event>& out) {
  out.clear();
  // TODO: calculate step boundaries based on BPM and sample rate
  // TODO: emit events when crossing a step boundary
}

void Sequencer::getPosition(uint16_t& bar, uint8_t& step) const {
  bar = m_currentBar;
  step = m_currentStep;
}

void Sequencer::setPosition(uint16_t bar, uint8_t step) {
  m_currentBar = bar;
  m_currentStep = step;
  m_sampleCounter = 0;
}

} // namespace rb338
