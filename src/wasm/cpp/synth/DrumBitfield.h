#pragma once

#include <cstdint>

namespace rb338 {

// A drum StepData names *instruments*, not row bytes: the parser maps each
// machine's 12-byte step row (RbsFormat.md §6e, accent first) onto these
// bits, so BD..OH, CP and RS mean the same sound on the 808 and the 909. The
// two bits the machines do not share carry one instrument each: CB is the
// 808 cowbell / 909 crash, CY the 808 cymbal / 909 ride.

/** Primary drum hits packed into `StepData.note` (bits 0–7). */
namespace DrumHit {
constexpr uint8_t BD = 0x01;
constexpr uint8_t SD = 0x02;
constexpr uint8_t LT = 0x04;
constexpr uint8_t MT = 0x08;
constexpr uint8_t HT = 0x10;
constexpr uint8_t CH = 0x20;
constexpr uint8_t OH = 0x40;
constexpr uint8_t CB = 0x80; // 808 cowbell / 909 crash cymbal
} // namespace DrumHit

/** Secondary drum hits packed into `StepData.drumExtra` (bits 0–2). */
namespace DrumExtra {
constexpr uint8_t CP = 0x01; // hand clap (808: or maracas, per the CP/MA selector)
constexpr uint8_t CY = 0x02; // 808 cymbal / 909 ride cymbal
constexpr uint8_t RS = 0x04; // rim shot (808: or claves, per the RS/CL selector)
} // namespace DrumExtra

} // namespace rb338
