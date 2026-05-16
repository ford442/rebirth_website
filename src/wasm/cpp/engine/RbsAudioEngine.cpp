#include "RbsAudioEngine.h"
#include <algorithm>
#include <cstring>

namespace rb338 {

RbsAudioEngine::RbsAudioEngine() = default;
RbsAudioEngine::~RbsAudioEngine() = default;

bool RbsAudioEngine::init(const EngineConfig& config) {
  m_config = config;
  m_initialised = true;
  m_currentBar = 1;
  m_currentStep = 0;
  return true;
}

bool RbsAudioEngine::loadSong(const ParsedSong& song) {
  if (!m_initialised) return false;
  m_currentBar = 1;
  m_currentStep = 0;
  // TODO: initialise sequencer, mixer, and voices from parsed song
  return true;
}

void RbsAudioEngine::play() {
  m_playing = true;
}

void RbsAudioEngine::pause() {
  m_playing = false;
}

void RbsAudioEngine::stop() {
  m_playing = false;
  m_currentBar = 1;
  m_currentStep = 0;
}

void RbsAudioEngine::seek(uint16_t bar) {
  m_currentBar = std::max<uint16_t>(1, bar);
  m_currentStep = 0;
}

bool RbsAudioEngine::isPlaying() const {
  return m_playing;
}

void RbsAudioEngine::getPlaybackPosition(uint16_t& bar, uint8_t& step) const {
  bar = m_currentBar;
  step = m_currentStep;
}

void RbsAudioEngine::processBlock(float* const* outputBuffers,
                                   uint32_t numChannels,
                                   uint32_t numFrames) {
  // TODO: real synthesis
  // For now, output silence so the audio graph stays alive
  for (uint32_t ch = 0; ch < numChannels; ++ch) {
    if (outputBuffers[ch]) {
      std::memset(outputBuffers[ch], 0, numFrames * sizeof(float));
    }
  }

  // Advance dummy position for UI testing
  if (m_playing) {
    m_currentStep++;
    if (m_currentStep >= 16) {
      m_currentStep = 0;
      m_currentBar++;
    }
  }
}

} // namespace rb338
