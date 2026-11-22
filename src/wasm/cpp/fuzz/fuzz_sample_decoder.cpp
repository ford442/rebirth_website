// libFuzzer target: probeSample + decodeSampleMono (AIFF / AIFF-C / WAV).
//
// The destination is deliberately small and fixed: the decoder must honour
// its capacity and report DestinationTooSmall rather than write past it.

#include "../audio/SampleDecoder.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  rb338::SampleInfo info;
  (void)rb338::probeSample(data, size, info);

  static std::array<float, 4096> dest;
  rb338::SampleInfo decoded;
  const rb338::SampleDecodeStatus status =
      rb338::decodeSampleMono(data, size, dest.data(), dest.size(), decoded);
  if (status == rb338::SampleDecodeStatus::Ok) {
    if (decoded.frameCount > dest.size()) std::abort();
    for (uint32_t i = 0; i < decoded.frameCount; ++i) {
      if (!std::isfinite(dest[i])) std::abort(); // decoders must not emit NaN/inf
    }
  }
  return 0;
}
