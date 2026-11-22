#pragma once

// DrumKit — names for the TR-808 / TR-909 kit bytes in DeviceState::kit.
//
// Each name is the TRAK controller id on that machine's track, which is also
// the byte offset in its device-chunk header: 0x00 enabled, 0x01 selected
// pattern, 0x02 accent level (DeviceState::accent), then the kit from 0x03.
// The layout is Propellerhead's RBS 4.2 controller list (as transcribed by
// the jsynth reference parser) and matches every chunk in the archive corpus
// (RbsFormat.md §6c/§6d).

#include "RbsTypes.h"

#include <cstddef>
#include <cstdint>

namespace rb338 {

/** First kit controller id; `kit[id - KIT_FIRST_CONTROLLER]`. */
constexpr uint8_t KIT_FIRST_CONTROLLER = 0x03;
/** The drum machines' ACCENT knob: controller 0x02, header byte 2. */
constexpr uint8_t KIT_ACCENT_CONTROLLER = 0x02;

constexpr size_t kitIndex(uint8_t controller) {
  return static_cast<size_t>(controller - KIT_FIRST_CONTROLLER);
}

namespace Kit808 {
enum Controller : uint8_t {
  BassLevel = 0x03, BassTone, BassDecay,
  SnareLevel, SnareTone, SnareSnappy,
  LowTomLevel, LowTomTuning, LowTomSelector,
  MidTomLevel, MidTomTuning, MidTomSelector,
  HiTomLevel, HiTomTuning, HiTomSelector,
  RimShotLevel, RimShotSelector,
  ClapLevel, ClapSelector,
  CowBellLevel,
  CymbalLevel, CymbalTone, CymbalDecay,
  OpenHatLevel, OpenHatDecay,
  ClosedHatLevel,
  Unknown1d, // 0x1d: present in every chunk, meaning not established
  End,
};
/** 808 chunk header: enabled, pattern, accent, then the kit. */
constexpr size_t kHeaderBytes = End;
static_assert(kHeaderBytes == 30, "808 header is 30 bytes");
} // namespace Kit808

namespace Kit909 {
enum Controller : uint8_t {
  BassLevel = 0x03, BassTune, BassAttack, BassDecay,
  SnareLevel, SnareTune, SnareTone, SnareSnappy,
  LowTomLevel, LowTomTune, LowTomDecay,
  MidTomLevel, MidTomTune, MidTomDecay,
  HiTomLevel, HiTomTune, HiTomDecay,
  RimShotLevel,
  ClapLevel,
  HiHatLevel, ClosedHatDecay, OpenHatDecay,
  CrashLevel, CrashTune,
  RideLevel, RideTune,
  FlamInterval,
  Unknown1e, // 0x1e: present in every chunk, meaning not established
  End,
};
constexpr size_t kHeaderBytes = End;
static_assert(kHeaderBytes == 31, "909 header is 31 bytes");
} // namespace Kit909

static_assert(Kit909::End - KIT_FIRST_CONTROLLER == DRUM_KIT_BYTES,
              "DeviceState::kit holds the 909's 28 kit bytes");
static_assert(Kit808::End - KIT_FIRST_CONTROLLER <= DRUM_KIT_BYTES);

/** Kit bytes the device's chunk actually carries (27 on the 808, 28 on the 909). */
constexpr size_t kitBytesFor(DeviceId device) {
  return device == DeviceId::TR909 ? Kit909::End - KIT_FIRST_CONTROLLER
                                   : Kit808::End - KIT_FIRST_CONTROLLER;
}

} // namespace rb338
