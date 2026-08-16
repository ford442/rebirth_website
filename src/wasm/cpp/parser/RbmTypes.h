#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rb338 {

enum class ModResourceKind : uint8_t {
  Sample = 0,
  Skin,
  Song,
  Other,
};

enum class ModSampleSlot : uint8_t {
  Unknown = 0,
  Tr808Kick,
  Tr808Snare,
  Tr808LowTom,
  Tr808MidTom,
  Tr808HighTom,
  Tr808ClosedHat,
  Tr808OpenHat,
  Tr808Rimshot,
  Tr808Clap,
  Tr808Clave,
  Tr808Cymbal,
  Tr808Maracas,
  Tr909Kick,
  Tr909Snare,
  Tr909LowTom,
  Tr909MidTom,
  Tr909HighTom,
  Tr909ClosedHat,
  Tr909OpenHat,
  Tr909Rimshot,
  Tr909Clap,
  Tr909Crash,
  Tr909Ride,
  Tb303Saw,
  Tb303Square,
};

struct EmbeddedResource {
  std::string name;
  ModResourceKind kind = ModResourceKind::Other;
  ModSampleSlot slot = ModSampleSlot::Unknown;
  std::vector<uint8_t> bytes;
};

struct ParsedMod {
  std::string title;
  std::string description;
  std::string copyright;
  std::vector<EmbeddedResource> resources;
};

inline const char* modSampleSlotName(ModSampleSlot slot) {
  switch (slot) {
    case ModSampleSlot::Tr808Kick: return "tr808-kick";
    case ModSampleSlot::Tr808Snare: return "tr808-snare";
    case ModSampleSlot::Tr808LowTom: return "tr808-low-tom";
    case ModSampleSlot::Tr808MidTom: return "tr808-mid-tom";
    case ModSampleSlot::Tr808HighTom: return "tr808-high-tom";
    case ModSampleSlot::Tr808ClosedHat: return "tr808-closed-hat";
    case ModSampleSlot::Tr808OpenHat: return "tr808-open-hat";
    case ModSampleSlot::Tr808Rimshot: return "tr808-rimshot";
    case ModSampleSlot::Tr808Clap: return "tr808-clap";
    case ModSampleSlot::Tr808Clave: return "tr808-clave";
    case ModSampleSlot::Tr808Cymbal: return "tr808-cymbal";
    case ModSampleSlot::Tr808Maracas: return "tr808-maracas";
    case ModSampleSlot::Tr909Kick: return "tr909-kick";
    case ModSampleSlot::Tr909Snare: return "tr909-snare";
    case ModSampleSlot::Tr909LowTom: return "tr909-low-tom";
    case ModSampleSlot::Tr909MidTom: return "tr909-mid-tom";
    case ModSampleSlot::Tr909HighTom: return "tr909-high-tom";
    case ModSampleSlot::Tr909ClosedHat: return "tr909-closed-hat";
    case ModSampleSlot::Tr909OpenHat: return "tr909-open-hat";
    case ModSampleSlot::Tr909Rimshot: return "tr909-rimshot";
    case ModSampleSlot::Tr909Clap: return "tr909-clap";
    case ModSampleSlot::Tr909Crash: return "tr909-crash";
    case ModSampleSlot::Tr909Ride: return "tr909-ride";
    case ModSampleSlot::Tb303Saw: return "tb303-saw";
    case ModSampleSlot::Tb303Square: return "tb303-square";
    case ModSampleSlot::Unknown: return "unknown";
  }
  return "unknown";
}

} // namespace rb338
