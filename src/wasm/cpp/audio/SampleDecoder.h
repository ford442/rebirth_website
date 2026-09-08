#pragma once

#include <cstddef>
#include <cstdint>

namespace rb338 {

/** Why a sample payload could not be turned into PCM. */
enum class SampleDecodeStatus : uint8_t {
  Ok = 0,
  UnknownFormat,       // magic bytes are neither AIFF nor RIFF/WAVE
  Malformed,           // truncated or self-inconsistent chunk headers
  UnsupportedEncoding, // e.g. a compressed AIFF-C variant we do not decode
  Empty,               // headers parsed but there are zero frames
  DestinationTooSmall, // caller's arena cannot hold frameCount floats
};

const char* sampleDecodeStatusName(SampleDecodeStatus status);

/** Header-level description of a sample payload. Filled in by probe/decode. */
struct SampleInfo {
  uint32_t frameCount = 0;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint8_t bitDepth = 0;
};

/**
 * Read headers only — no PCM conversion, no allocation.
 *
 * Lets a caller size (or reject) its arena before committing to a decode.
 */
SampleDecodeStatus probeSample(const uint8_t* data, size_t size, SampleInfo& out);

/**
 * Decode to mono float32 in [-1, 1] written into `dest`.
 *
 * Multi-channel sources are averaged down to mono (ReBirth slots are mono).
 * `destFrames` is the capacity of `dest` in floats; the decode fails with
 * DestinationTooSmall rather than writing past it. Allocation-free: this is
 * still main-thread-only work, but it never grows a buffer mid-decode.
 */
SampleDecodeStatus decodeSampleMono(const uint8_t* data, size_t size, float* dest,
                                    size_t destFrames, SampleInfo& out);

// ── Per-format entry points (dispatched by decodeSampleMono/probeSample) ──

SampleDecodeStatus probeAiff(const uint8_t* data, size_t size, SampleInfo& out);
SampleDecodeStatus decodeAiffMono(const uint8_t* data, size_t size, float* dest,
                                  size_t destFrames, SampleInfo& out);

SampleDecodeStatus probeWav(const uint8_t* data, size_t size, SampleInfo& out);
SampleDecodeStatus decodeWavMono(const uint8_t* data, size_t size, float* dest,
                                 size_t destFrames, SampleInfo& out);

} // namespace rb338
