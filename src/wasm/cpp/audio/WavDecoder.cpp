/**
 * WavDecoder — RIFF/WAVE support on top of the vendored dr_wav (public
 * domain / MIT-0, see third_party/dr_wav.h).
 *
 * WAV is the rare case in ReBirth mods (ModPacker wrote AIFF), but dr_wav
 * covers the long tail — 8/24/32-bit, float, ADPCM — that a hand-rolled
 * reader would silently get wrong.
 *
 * dr_wav may allocate internally while parsing headers. That is fine here:
 * decoding only ever happens on the main thread, never inside processBlock().
 */

#define DR_WAV_NO_STDIO
#include "../third_party/dr_wav.h"

#include "SampleDecoder.h"
#include <cmath>

namespace rb338 {

namespace {

/** Frames of interleaved float pulled from dr_wav per iteration. */
constexpr size_t kChunkFrames = 512;
constexpr size_t kMaxChannels = 8;

} // anonymous namespace

SampleDecodeStatus probeWav(const uint8_t* data, size_t size, SampleInfo& out) {
  if (!data || size < 12) return SampleDecodeStatus::UnknownFormat;

  drwav wav;
  if (!drwav_init_memory(&wav, data, size, nullptr)) {
    return SampleDecodeStatus::UnknownFormat;
  }

  out.channels = static_cast<uint8_t>(wav.channels > 255u ? 255u : wav.channels);
  out.sampleRate = wav.sampleRate;
  out.bitDepth = static_cast<uint8_t>(wav.bitsPerSample > 255u ? 255u : wav.bitsPerSample);
  out.frameCount = (wav.totalPCMFrameCount > 0xffffffffull)
                       ? 0xffffffffu
                       : static_cast<uint32_t>(wav.totalPCMFrameCount);

  drwav_uninit(&wav);

  if (out.channels == 0) return SampleDecodeStatus::Malformed;
  if (out.frameCount == 0) return SampleDecodeStatus::Empty;
  return SampleDecodeStatus::Ok;
}

SampleDecodeStatus decodeWavMono(const uint8_t* data, size_t size, float* dest,
                                 size_t destFrames, SampleInfo& out) {
  if (!data || size < 12) return SampleDecodeStatus::UnknownFormat;

  drwav wav;
  if (!drwav_init_memory(&wav, data, size, nullptr)) {
    return SampleDecodeStatus::UnknownFormat;
  }

  const uint32_t channels = wav.channels;
  out.channels = static_cast<uint8_t>(channels > 255u ? 255u : channels);
  out.sampleRate = wav.sampleRate;
  out.bitDepth = static_cast<uint8_t>(wav.bitsPerSample > 255u ? 255u : wav.bitsPerSample);
  out.frameCount = (wav.totalPCMFrameCount > 0xffffffffull)
                       ? 0xffffffffu
                       : static_cast<uint32_t>(wav.totalPCMFrameCount);

  if (channels == 0 || channels > kMaxChannels) {
    drwav_uninit(&wav);
    return SampleDecodeStatus::UnsupportedEncoding;
  }
  if (out.frameCount == 0) {
    drwav_uninit(&wav);
    return SampleDecodeStatus::Empty;
  }
  if (!dest || destFrames < out.frameCount) {
    drwav_uninit(&wav);
    return SampleDecodeStatus::DestinationTooSmall;
  }

  float interleaved[kChunkFrames * kMaxChannels];
  const float invChannels = 1.0f / static_cast<float>(channels);
  size_t written = 0;

  while (written < out.frameCount) {
    const size_t want = (out.frameCount - written < kChunkFrames) ? (out.frameCount - written)
                                                                  : kChunkFrames;
    const drwav_uint64 got = drwav_read_pcm_frames_f32(&wav, want, interleaved);
    if (got == 0) break;

    for (size_t frame = 0; frame < static_cast<size_t>(got); ++frame) {
      float sum = 0.0f;
      for (size_t ch = 0; ch < channels; ++ch) {
        sum += interleaved[frame * channels + ch];
      }
      const float value = sum * invChannels;
      dest[written + frame] = std::isfinite(value) ? value : 0.0f;
    }
    written += static_cast<size_t>(got);
  }

  drwav_uninit(&wav);

  if (written == 0) return SampleDecodeStatus::Empty;
  // A short read means the data chunk was truncated; keep what we decoded.
  out.frameCount = static_cast<uint32_t>(written);
  return SampleDecodeStatus::Ok;
}

} // namespace rb338
