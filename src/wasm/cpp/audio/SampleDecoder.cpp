/**
 * SampleDecoder — format dispatch for embedded mod samples.
 *
 * Chooses a decoder from the payload's magic bytes rather than the `EMBF`
 * filename extension: ModPacker's naming is inconsistent across packs, and
 * the bytes are authoritative.
 */

#include "SampleDecoder.h"

namespace rb338 {

namespace {

bool startsWith(const uint8_t* data, size_t size, const char (&id)[5]) {
  if (size < 4) return false;
  return data[0] == static_cast<uint8_t>(id[0]) && data[1] == static_cast<uint8_t>(id[1]) &&
         data[2] == static_cast<uint8_t>(id[2]) && data[3] == static_cast<uint8_t>(id[3]);
}

bool looksLikeAiff(const uint8_t* data, size_t size) {
  return startsWith(data, size, "FORM");
}

bool looksLikeWav(const uint8_t* data, size_t size) {
  // RIFF (little-endian) and RIFX (big-endian) both reach dr_wav.
  return startsWith(data, size, "RIFF") || startsWith(data, size, "RIFX");
}

} // anonymous namespace

const char* sampleDecodeStatusName(SampleDecodeStatus status) {
  switch (status) {
    case SampleDecodeStatus::Ok: return "ok";
    case SampleDecodeStatus::UnknownFormat: return "unknown-format";
    case SampleDecodeStatus::Malformed: return "malformed";
    case SampleDecodeStatus::UnsupportedEncoding: return "unsupported-encoding";
    case SampleDecodeStatus::Empty: return "empty";
    case SampleDecodeStatus::DestinationTooSmall: return "destination-too-small";
  }
  return "unknown";
}

SampleDecodeStatus probeSample(const uint8_t* data, size_t size, SampleInfo& out) {
  out = SampleInfo{};
  if (!data || size < 12) return SampleDecodeStatus::UnknownFormat;
  if (looksLikeAiff(data, size)) return probeAiff(data, size, out);
  if (looksLikeWav(data, size)) return probeWav(data, size, out);
  return SampleDecodeStatus::UnknownFormat;
}

SampleDecodeStatus decodeSampleMono(const uint8_t* data, size_t size, float* dest,
                                    size_t destFrames, SampleInfo& out) {
  out = SampleInfo{};
  if (!data || size < 12) return SampleDecodeStatus::UnknownFormat;
  if (looksLikeAiff(data, size)) return decodeAiffMono(data, size, dest, destFrames, out);
  if (looksLikeWav(data, size)) return decodeWavMono(data, size, dest, destFrames, out);
  return SampleDecodeStatus::UnknownFormat;
}

} // namespace rb338
