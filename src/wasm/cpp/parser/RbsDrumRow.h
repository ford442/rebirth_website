#pragma once

// RbsDrumRow — where each instrument sits in an 808 / 909 step row.
//
// A drum step is 12 bytes in the order of the original instruments'
// instrument-select dial, accent first (RbsFormat.md §6e):
//
//   808:  AC BD SD LT MT HT RS CP CB CY OH CH
//   909:  AC BD SD LT MT HT RS CP CH OH CR RD
//
// The parser and the writer share this table so a row byte can only ever be
// read and written at the same offset.

#include <array>
#include <cstddef>
#include <cstdint>

namespace rb338::detail {

constexpr size_t DRUM_ROW_BYTES = 12;
constexpr size_t DRUM_ROW_ACCENT = 0;
/** Instruments per row: 11, in DrumHitIndex order (see DrumMachineVoice.h). */
constexpr size_t DRUM_ROW_HITS = 11;

/** Row byte per hit: BD SD LT MT HT CH OH CB CP CY RS. */
constexpr std::array<uint8_t, DRUM_ROW_HITS> kDrumRow808 = {1, 2, 3, 4, 5, 11, 10, 8, 7, 9, 6};
constexpr std::array<uint8_t, DRUM_ROW_HITS> kDrumRow909 = {1, 2, 3, 4, 5, 8, 9, 10, 7, 11, 6};

/** 909 row values. The 808 stores only 0/1. */
constexpr uint8_t DRUM_ROW_OFF = 0;
constexpr uint8_t DRUM_ROW_HIT = 1;
constexpr uint8_t DRUM_ROW_ACCENTED = 2;
constexpr uint8_t DRUM_ROW_FLAM = 3;

/** Bit `hit` of the combined note | drumExtra << 8 word. */
constexpr uint32_t hitBit(size_t hit) { return 1u << hit; }

} // namespace rb338::detail
