#pragma once

#include "DrumMachineVoice.h"

namespace rb338 {

/**
 * TR-808 kit mapping. Tr808Cymbal has no channel (there is no sequencer bit
 * for it), so a mod's cymbal sample is catalogued but never triggered.
 */
struct Tr808Kit {
  static constexpr bool kIs909 = false;

  enum Channel : uint8_t {
    Kick, Snare, ClosedHat, OpenHat, Rimshot, Clap, Clave, Maracas,
    LowTom, MidTom, HighTom, Count
  };

  static constexpr std::array<ModSampleSlot, Count> kSlots = {
      ModSampleSlot::Tr808Kick,     ModSampleSlot::Tr808Snare,
      ModSampleSlot::Tr808ClosedHat, ModSampleSlot::Tr808OpenHat,
      ModSampleSlot::Tr808Rimshot,  ModSampleSlot::Tr808Clap,
      ModSampleSlot::Tr808Clave,    ModSampleSlot::Tr808Maracas,
      ModSampleSlot::Tr808LowTom,   ModSampleSlot::Tr808MidTom,
      ModSampleSlot::Tr808HighTom,
  };

  // RbsFormat.md §6e: byte 8 (CL) is the claves, byte 9 (CP) the clap and
  // byte 10 (MA) the maracas on the 808.
  static constexpr std::array<DrumRoute, kNumDrumHits> kRoutes = {{
      {Kick, DrumVoiceId::Kick},           // BD
      {Snare, DrumVoiceId::Snare},         // SD
      {LowTom, DrumVoiceId::LowTom},       // LT
      {MidTom, DrumVoiceId::MidTom},       // MT
      {HighTom, DrumVoiceId::HighTom},     // HT
      {ClosedHat, DrumVoiceId::ClosedHat}, // CH
      {OpenHat, DrumVoiceId::OpenHat},     // OH
      {Clave, DrumVoiceId::Clave},         // CL
      {Clap, DrumVoiceId::Clap},           // CP
      {Maracas, DrumVoiceId::Maracas},     // MA
      {Rimshot, DrumVoiceId::Rimshot},     // RS
  }};
};

using Tr808Voice = DrumMachineVoice<Tr808Kit>;
extern template class DrumMachineVoice<Tr808Kit>;

} // namespace rb338
