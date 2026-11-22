#pragma once

#include "DrumMachineVoice.h"

namespace rb338 {

/** TR-909 kit mapping: every voice, hand clap included. */
struct Tr909Kit {
  static constexpr bool kIs909 = true;

  enum Channel : uint8_t {
    Kick, Snare, ClosedHat, OpenHat, Clap, Rimshot, Crash, Ride,
    LowTom, MidTom, HighTom, Count
  };

  static constexpr std::array<ModSampleSlot, Count> kSlots = {
      ModSampleSlot::Tr909Kick,     ModSampleSlot::Tr909Snare,
      ModSampleSlot::Tr909ClosedHat, ModSampleSlot::Tr909OpenHat,
      ModSampleSlot::Tr909Clap,     ModSampleSlot::Tr909Rimshot,
      ModSampleSlot::Tr909Crash,    ModSampleSlot::Tr909Ride,
      ModSampleSlot::Tr909LowTom,   ModSampleSlot::Tr909MidTom,
      ModSampleSlot::Tr909HighTom,
  };

  // RbsFormat.md §6e: byte 8 (CL) is the hand clap, byte 9 (CP) the crash and
  // byte 10 (MA) the ride on the 909. The CL bit used to be ignored, so the
  // clap channel (and a mod's Tr909Clap sample) could never sound.
  static constexpr std::array<DrumRoute, kNumDrumHits> kRoutes = {{
      {Kick, DrumVoiceId::Kick},           // BD
      {Snare, DrumVoiceId::Snare},         // SD
      {LowTom, DrumVoiceId::LowTom},       // LT
      {MidTom, DrumVoiceId::MidTom},       // MT
      {HighTom, DrumVoiceId::HighTom},     // HT
      {ClosedHat, DrumVoiceId::ClosedHat}, // CH
      {OpenHat, DrumVoiceId::OpenHat},     // OH
      {Clap, DrumVoiceId::Clap},           // CL
      {Crash, DrumVoiceId::Crash},         // CP
      {Ride, DrumVoiceId::Ride},           // MA
      {Rimshot, DrumVoiceId::Rimshot},     // RS
  }};
};

using Tr909Voice = DrumMachineVoice<Tr909Kit>;
extern template class DrumMachineVoice<Tr909Kit>;

} // namespace rb338
