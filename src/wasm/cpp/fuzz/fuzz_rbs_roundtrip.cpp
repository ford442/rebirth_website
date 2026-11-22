// libFuzzer target: parse → RbsWriter → parse must be semantically equal.
//
// Any input the parser accepts has to survive a save: the reparse must
// succeed and match field for field (RbsCompare.h). A difference aborts, so
// the fuzzer saves the input as a crash for triage.

#include "../parser/RbsCompare.h"
#include "../parser/RbsParser.h"
#include "../parser/RbsWriter.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  rb338::RbsParser parser;
  auto song = parser.parse(data, size);
  if (!song) return 0;

  rb338::RbsWriter writer;
  auto bytes = writer.write(*song);
  if (!bytes) return 0; // a song the writer refuses (e.g. out-of-range automation) is fine

  rb338::RbsParser reparser;
  auto reloaded = reparser.parse(bytes->data(), bytes->size());
  if (!reloaded) {
    std::fprintf(stderr, "written song does not reparse: %s\n", reparser.lastError().c_str());
    std::abort();
  }
  const std::string diff = rb338::semanticDifference(*song, *reloaded);
  if (!diff.empty()) {
    std::fprintf(stderr, "round trip changed: %s\n", diff.c_str());
    std::abort();
  }
  return 0;
}
