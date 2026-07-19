#pragma once

#include <cstdint>

namespace rb338 {

/** Primary drum hits packed into `StepData.note` (bits 0–7). */
namespace DrumHit {
constexpr uint8_t BD = 0x01;
constexpr uint8_t SD = 0x02;
constexpr uint8_t LT = 0x04;
constexpr uint8_t MT = 0x08;
constexpr uint8_t HT = 0x10;
constexpr uint8_t CH = 0x20;
constexpr uint8_t OH = 0x40;
constexpr uint8_t CL = 0x80;
} // namespace DrumHit

/** Secondary drum hits packed into `StepData.drumExtra` (bits 0–2). */
namespace DrumExtra {
constexpr uint8_t CP = 0x01; // byte 9 — clap / crash
constexpr uint8_t MA = 0x02; // byte 10 — maracas / ride (Phase 2)
constexpr uint8_t RS = 0x04; // byte 11 — rimshot
} // namespace DrumExtra

} // namespace rb338
