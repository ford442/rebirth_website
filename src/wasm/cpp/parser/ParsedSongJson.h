#pragma once

#include "RbsTypes.h"
#include <string>

namespace rb338 {

/** Serialize a ParsedSong to compact JSON (WasmParsedSong-compatible shape). */
std::string parsedSongToJson(const ParsedSong& song);

} // namespace rb338
