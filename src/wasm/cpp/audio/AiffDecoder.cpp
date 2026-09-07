/**
 * AiffDecoder — minimal IFF/AIFF + AIFF-C reader for ReBirth mod samples.
 *
 * ModPacker wrote plain `FORM`/`AIFF` with 16-bit big-endian PCM, which is
 * the path that matters. We also accept AIFF-C with `NONE` (big-endian PCM),
 * `sowt` (little-endian PCM) and `fl32`/`FL32` (32-bit float) because Mac
 * tooling of the era emitted those, and reject every other compression type
 * rather than guessing.
 *
 * dr_wav does not cover AIFF, hence this file. No exceptions (the engine has
 * an -fno-exceptions CI leg) and no allocation: the caller supplies the
 * destination buffer.
 */

#include "SampleDecoder.h"
#include <cmath>
#include <cstring>

namespace rb338 {

namespace {

constexpr size_t kChunkHeaderBytes = 8;

uint16_t readU16Be(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t readU32Be(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool idEquals(const uint8_t* p, const char (&id)[5]) {
  return p[0] == static_cast<uint8_t>(id[0]) && p[1] == static_cast<uint8_t>(id[1]) &&
         p[2] == static_cast<uint8_t>(id[2]) && p[3] == static_cast<uint8_t>(id[3]);
}

/** 80-bit IEEE 754 extended precision, as used by AIFF's COMM sample rate. */
double readExtended80(const uint8_t* p) {
  const uint16_t rawExponent = readU16Be(p);
  const double sign = (rawExponent & 0x8000u) ? -1.0 : 1.0;
  const int exponent = static_cast<int>(rawExponent & 0x7fffu);

  uint64_t mantissa = 0;
  for (int i = 0; i < 8; ++i) {
    mantissa = (mantissa << 8) | static_cast<uint64_t>(p[2 + i]);
  }

  if (exponent == 0 && mantissa == 0) return 0.0;
  if (exponent == 0x7fff) return 0.0; // Inf/NaN — treat as "unknown rate"

  // The leading integer bit is explicit in this format, so the mantissa is
  // scaled by 2^63 rather than 2^52 as in a double.
  return sign * std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
}

enum class AiffEncoding : uint8_t {
  PcmBigEndian,
  PcmLittleEndian,
  Float32BigEndian,
  Unsupported,
};

struct AiffLayout {
  SampleInfo info;
  AiffEncoding encoding = AiffEncoding::PcmBigEndian;
  size_t pcmOffset = 0; // absolute offset of the first sample byte
  size_t pcmBytes = 0;  // usable bytes from pcmOffset
};

AiffEncoding encodingFromCompression(const uint8_t* id) {
  if (idEquals(id, "NONE")) return AiffEncoding::PcmBigEndian;
  if (idEquals(id, "sowt")) return AiffEncoding::PcmLittleEndian;
  if (idEquals(id, "fl32") || idEquals(id, "FL32")) return AiffEncoding::Float32BigEndian;
  return AiffEncoding::Unsupported;
}

/** Walk the chunk list once, collecting COMM + SSND. */
SampleDecodeStatus parseAiffLayout(const uint8_t* data, size_t size, AiffLayout& out) {
  if (!data || size < 12) return SampleDecodeStatus::UnknownFormat;
  if (!idEquals(data, "FORM")) return SampleDecodeStatus::UnknownFormat;

  const bool isAifc = idEquals(data + 8, "AIFC");
  if (!idEquals(data + 8, "AIFF") && !isAifc) return SampleDecodeStatus::UnknownFormat;

  // FORM's declared size covers everything after the size field; clamp to the
  // buffer so a truncated or over-declared file cannot walk us off the end.
  const uint32_t formSize = readU32Be(data + 4);
  size_t limit = size;
  if (formSize <= size - 8) limit = static_cast<size_t>(formSize) + 8;

  bool haveComm = false;
  bool haveSsnd = false;
  size_t cursor = 12;

  while (cursor + kChunkHeaderBytes <= limit) {
    const uint8_t* header = data + cursor;
    const uint32_t chunkSize = readU32Be(header + 4);
    const size_t body = cursor + kChunkHeaderBytes;

    // A chunk may declare more bytes than the file actually holds. Work
    // from what is present rather than rejecting outright; each handler
    // below still enforces its own minimum.
    const size_t available = limit - body;
    const size_t usable = (chunkSize <= available) ? static_cast<size_t>(chunkSize) : available;

    if (idEquals(header, "COMM")) {
      if (usable < 18) return SampleDecodeStatus::Malformed;
      // COMM layout: numChannels(2) numSampleFrames(4) sampleSize(2)
      // sampleRate(10 as 80-bit extended) [AIFF-C: compressionType(4)].
      const uint8_t* comm = data + body;
      const uint16_t channels = readU16Be(comm);
      const uint16_t sampleSize = readU16Be(comm + 6);
      if (channels == 0 || channels > 255) return SampleDecodeStatus::Malformed;
      if (sampleSize != 8 && sampleSize != 16 && sampleSize != 24 && sampleSize != 32) {
        return SampleDecodeStatus::UnsupportedEncoding;
      }

      out.info.channels = static_cast<uint8_t>(channels);
      out.info.frameCount = readU32Be(comm + 2);
      out.info.bitDepth = static_cast<uint8_t>(sampleSize);
      const double rate = readExtended80(comm + 8);
      out.info.sampleRate = (rate > 0.0 && rate < 4.0e9) ? static_cast<uint32_t>(rate) : 0u;

      if (isAifc) {
        if (usable < 22) return SampleDecodeStatus::Malformed;
        out.encoding = encodingFromCompression(comm + 18);
        if (out.encoding == AiffEncoding::Unsupported) {
          return SampleDecodeStatus::UnsupportedEncoding;
        }
        if (out.encoding == AiffEncoding::Float32BigEndian && out.info.bitDepth != 32) {
          return SampleDecodeStatus::Malformed;
        }
      }
      haveComm = true;
    } else if (idEquals(header, "SSND")) {
      if (usable < 8) return SampleDecodeStatus::Malformed;
      const uint32_t soundOffset = readU32Be(data + body);
      if (soundOffset > usable - 8) return SampleDecodeStatus::Malformed;
      // A truncated SSND is common in salvaged archive files; keep the
      // frames that are actually there instead of rejecting the sample.
      out.pcmOffset = body + 8 + soundOffset;
      out.pcmBytes = usable - 8 - soundOffset;
      haveSsnd = true;
    }

    // Chunks are word-aligned: an odd size is followed by a pad byte.
    const size_t advance = kChunkHeaderBytes + chunkSize + (chunkSize & 1u);
    if (advance == 0 || advance > limit - cursor) break;
    cursor += advance;
  }

  if (!haveComm || !haveSsnd) return SampleDecodeStatus::Malformed;
  if (out.info.channels == 0) return SampleDecodeStatus::Malformed;

  const bool depthOk = out.info.bitDepth == 8 || out.info.bitDepth == 16 ||
                       out.info.bitDepth == 24 || out.info.bitDepth == 32;
  if (!depthOk) return SampleDecodeStatus::UnsupportedEncoding;

  // Trust the byte count over COMM's frame count when the chunk is short.
  const size_t bytesPerFrame =
      static_cast<size_t>(out.info.bitDepth / 8u) * static_cast<size_t>(out.info.channels);
  if (bytesPerFrame == 0) return SampleDecodeStatus::Malformed;
  const size_t framesAvailable = out.pcmBytes / bytesPerFrame;
  if (framesAvailable < out.info.frameCount) {
    out.info.frameCount = static_cast<uint32_t>(framesAvailable);
  }
  if (out.info.frameCount == 0) return SampleDecodeStatus::Empty;

  return SampleDecodeStatus::Ok;
}

float sampleToFloat(const uint8_t* p, uint8_t bitDepth, AiffEncoding encoding) {
  if (encoding == AiffEncoding::Float32BigEndian) {
    const uint32_t bits = readU32Be(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value) ? value : 0.0f;
  }

  const bool littleEndian = (encoding == AiffEncoding::PcmLittleEndian);
  switch (bitDepth) {
    case 8: {
      // AIFF 8-bit is signed (unlike RIFF/WAVE, which is unsigned).
      const auto v = static_cast<int8_t>(p[0]);
      return static_cast<float>(v) / 128.0f;
    }
    case 16: {
      const uint16_t raw = littleEndian
                               ? static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8))
                               : readU16Be(p);
      return static_cast<float>(static_cast<int16_t>(raw)) / 32768.0f;
    }
    case 24: {
      const uint32_t raw = littleEndian ? (static_cast<uint32_t>(p[0]) |
                                           (static_cast<uint32_t>(p[1]) << 8) |
                                           (static_cast<uint32_t>(p[2]) << 16))
                                        : ((static_cast<uint32_t>(p[0]) << 16) |
                                           (static_cast<uint32_t>(p[1]) << 8) |
                                           static_cast<uint32_t>(p[2]));
      // Sign-extend 24 -> 32 bits.
      const int32_t v = (raw & 0x800000u) ? static_cast<int32_t>(raw | 0xff000000u)
                                          : static_cast<int32_t>(raw);
      return static_cast<float>(v) / 8388608.0f;
    }
    case 32: {
      const uint32_t raw = littleEndian ? (static_cast<uint32_t>(p[0]) |
                                           (static_cast<uint32_t>(p[1]) << 8) |
                                           (static_cast<uint32_t>(p[2]) << 16) |
                                           (static_cast<uint32_t>(p[3]) << 24))
                                        : readU32Be(p);
      return static_cast<float>(static_cast<int32_t>(raw)) / 2147483648.0f;
    }
    default:
      return 0.0f;
  }
}

} // anonymous namespace

SampleDecodeStatus probeAiff(const uint8_t* data, size_t size, SampleInfo& out) {
  AiffLayout layout;
  const SampleDecodeStatus status = parseAiffLayout(data, size, layout);
  if (status == SampleDecodeStatus::Ok || layout.info.frameCount > 0) {
    out = layout.info;
  }
  return status;
}

SampleDecodeStatus decodeAiffMono(const uint8_t* data, size_t size, float* dest,
                                  size_t destFrames, SampleInfo& out) {
  AiffLayout layout;
  const SampleDecodeStatus status = parseAiffLayout(data, size, layout);
  if (status != SampleDecodeStatus::Ok) return status;
  out = layout.info;

  if (!dest) return SampleDecodeStatus::DestinationTooSmall;
  if (destFrames < layout.info.frameCount) return SampleDecodeStatus::DestinationTooSmall;

  const size_t bytesPerSample = layout.info.bitDepth / 8u;
  const size_t channels = layout.info.channels;
  const uint8_t* pcm = data + layout.pcmOffset;

  for (uint32_t frame = 0; frame < layout.info.frameCount; ++frame) {
    float sum = 0.0f;
    const uint8_t* framePtr = pcm + static_cast<size_t>(frame) * bytesPerSample * channels;
    for (size_t ch = 0; ch < channels; ++ch) {
      sum += sampleToFloat(framePtr + ch * bytesPerSample, layout.info.bitDepth, layout.encoding);
    }
    dest[frame] = sum / static_cast<float>(channels);
  }

  return SampleDecodeStatus::Ok;
}

} // namespace rb338
