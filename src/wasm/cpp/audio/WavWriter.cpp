#include "WavWriter.h"
#include <algorithm>
#include <cmath>

namespace rb338 {

namespace {

constexpr uint32_t kHeaderBytes = 44;

void pushLe16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void pushLe32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
  out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

void pushId(std::vector<uint8_t>& out, const char* id) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(id[i]));
}

int16_t toPcm16(float sample) {
  if (!std::isfinite(sample)) return 0;
  const float clamped = std::clamp(sample, -1.0f, 1.0f);
  // Asymmetric scaling: -1.0 maps to -32768 and +1.0 to +32767, so full-scale
  // positive peaks do not wrap.
  const float scaled = clamped < 0.0f ? clamped * 32768.0f : clamped * 32767.0f;
  return static_cast<int16_t>(std::lround(scaled));
}

} // anonymous namespace

uint32_t wavPcm16ByteSize(uint32_t frames, uint16_t channels) {
  return kHeaderBytes + frames * static_cast<uint32_t>(channels) * 2u;
}

WavPcm16Builder::WavPcm16Builder(uint32_t expectedFrames, uint16_t channels,
                                 uint32_t sampleRate)
    : m_channels(channels == 0 ? 1 : channels) {
  if (sampleRate == 0) sampleRate = 44100;

  // Header is written up front with the expected sizes and patched in take(),
  // so a short render still produces a valid file.
  m_bytes = writeWavPcm16(nullptr, 0, m_channels, sampleRate);
  m_bytes.resize(kHeaderBytes);
  m_bytes.reserve(wavPcm16ByteSize(expectedFrames, m_channels));
}

void WavPcm16Builder::append(const float* interleaved, uint32_t frames) {
  if (!interleaved || frames == 0) return;
  const uint32_t sampleCount = frames * static_cast<uint32_t>(m_channels);
  for (uint32_t i = 0; i < sampleCount; ++i) {
    pushLe16(m_bytes, static_cast<uint16_t>(toPcm16(interleaved[i])));
  }
  m_framesWritten += frames;
}

std::vector<uint8_t> WavPcm16Builder::take() {
  const uint32_t dataBytes =
      m_framesWritten * static_cast<uint32_t>(m_channels) * 2u;

  auto patchLe32 = [&](size_t offset, uint32_t value) {
    m_bytes[offset] = static_cast<uint8_t>(value & 0xffu);
    m_bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    m_bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
    m_bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
  };
  patchLe32(4, 36u + dataBytes);  // RIFF size
  patchLe32(40, dataBytes);       // data chunk size

  return std::move(m_bytes);
}

std::vector<uint8_t> writeWavPcm16(const float* interleaved, uint32_t frames,
                                   uint16_t channels, uint32_t sampleRate) {
  if (channels == 0) channels = 1;
  if (sampleRate == 0) sampleRate = 44100;

  const uint32_t sampleCount = frames * static_cast<uint32_t>(channels);
  const uint32_t dataBytes = sampleCount * 2u;
  const uint32_t byteRate = sampleRate * channels * 2u;
  const uint16_t blockAlign = static_cast<uint16_t>(channels * 2u);

  std::vector<uint8_t> out;
  out.reserve(kHeaderBytes + dataBytes);

  pushId(out, "RIFF");
  pushLe32(out, 36u + dataBytes); // size of everything after this field
  pushId(out, "WAVE");

  pushId(out, "fmt ");
  pushLe32(out, 16);       // PCM fmt chunk size
  pushLe16(out, 1);        // format tag: PCM
  pushLe16(out, channels);
  pushLe32(out, sampleRate);
  pushLe32(out, byteRate);
  pushLe16(out, blockAlign);
  pushLe16(out, 16);       // bits per sample

  pushId(out, "data");
  pushLe32(out, dataBytes);

  if (interleaved) {
    for (uint32_t i = 0; i < sampleCount; ++i) {
      pushLe16(out, static_cast<uint16_t>(toPcm16(interleaved[i])));
    }
  } else {
    out.resize(out.size() + dataBytes, 0);
  }

  return out;
}

} // namespace rb338
