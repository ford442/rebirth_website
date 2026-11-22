// libFuzzer target: RbmParser::parse + summariseMod on arbitrary bytes.
// summariseMod probes every sample payload, so this also reaches the
// AIFF/WAV header probes through a mod container.

#include "../parser/RbmParser.h"
#include "../synth/SamplePool.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  rb338::RbmParser parser;
  auto mod = parser.parse(data, size);
  if (mod) {
    const rb338::ModLoadReport report = rb338::summariseMod(*mod);
    (void)report;
  }
  return 0;
}
