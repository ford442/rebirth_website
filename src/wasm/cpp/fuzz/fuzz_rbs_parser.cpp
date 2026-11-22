// libFuzzer target: RbsParser::parse on arbitrary bytes.
//
// The parser reads untrusted input — a 5,610-song community corpus and
// whatever a visitor drags onto the player. It must reject, never crash,
// overrun or recurse without bound. Built only with -DRB338_FUZZ=ON (clang).

#include "../parser/RbsParser.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  rb338::RbsParser parser;
  auto song = parser.parse(data, size);
  if (song) {
    // Touch what the engine walks, so ASan sees any bad index the parser
    // produced (an arrangement ref outside the 32 slots, say).
    volatile size_t sink = song->patterns.size() + song->arrangement.size();
    for (const auto& bar : song->arrangement) {
      for (const auto& ref : bar.devicePatterns) sink = sink + ref.bank + ref.index;
    }
    (void)sink;
  }
  return 0;
}
