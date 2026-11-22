#pragma once

#include "DrumMachineVoice.h"

namespace rb338 {

/**
 * TR-808 kit. The three tom rows play congas when their selector is set,
 * RS/CL plays the claves and CP/MA the maracas — the 808's own switches,
 * saved in the kit bytes (DrumKit.h). The cowbell has no mod slot.
 */
struct Tr808Kit {
  static constexpr DeviceId kDevice = DeviceId::TR808;
  static constexpr bool kIs909 = false;

  enum Channel : uint8_t {
    Kick, Snare, ClosedHat, OpenHat, Rimshot, Clap, Clave, Maracas,
    LowTom, MidTom, HighTom, Cowbell, Cymbal, Count
  };
  static constexpr uint8_t kClosedHat = ClosedHat;
  static constexpr uint8_t kOpenHat = OpenHat;

  static constexpr std::array<ModSampleSlot, Count> kSlots = {
      ModSampleSlot::Tr808Kick,     ModSampleSlot::Tr808Snare,
      ModSampleSlot::Tr808ClosedHat, ModSampleSlot::Tr808OpenHat,
      ModSampleSlot::Tr808Rimshot,  ModSampleSlot::Tr808Clap,
      ModSampleSlot::Tr808Clave,    ModSampleSlot::Tr808Maracas,
      ModSampleSlot::Tr808LowTom,   ModSampleSlot::Tr808MidTom,
      ModSampleSlot::Tr808HighTom,  ModSampleSlot::Unknown,
      ModSampleSlot::Tr808Cymbal,
  };

  static DrumRoute route(size_t hit, const DrumKitBytes& kit) {
    using namespace Kit808;
    switch (hit) {
      case kHitBD: return {Kick, DrumVoiceId::Kick};
      case kHitSD: return {Snare, DrumVoiceId::Snare};
      case kHitLT:
        return kitByte(kit, LowTomSelector) ? DrumRoute{LowTom, DrumVoiceId::LowConga}
                                            : DrumRoute{LowTom, DrumVoiceId::LowTom};
      case kHitMT:
        return kitByte(kit, MidTomSelector) ? DrumRoute{MidTom, DrumVoiceId::MidConga}
                                            : DrumRoute{MidTom, DrumVoiceId::MidTom};
      case kHitHT:
        return kitByte(kit, HiTomSelector) ? DrumRoute{HighTom, DrumVoiceId::HighConga}
                                           : DrumRoute{HighTom, DrumVoiceId::HighTom};
      case kHitCH: return {ClosedHat, DrumVoiceId::ClosedHat};
      case kHitOH: return {OpenHat, DrumVoiceId::OpenHat};
      case kHitCB: return {Cowbell, DrumVoiceId::Cowbell};
      case kHitCP:
        return kitByte(kit, ClapSelector) ? DrumRoute{Clap, DrumVoiceId::Clap}
                                          : DrumRoute{Maracas, DrumVoiceId::Maracas};
      case kHitCY: return {Cymbal, DrumVoiceId::Cymbal};
      case kHitRS:
      default:
        return kitByte(kit, RimShotSelector) ? DrumRoute{Rimshot, DrumVoiceId::Rimshot}
                                             : DrumRoute{Clave, DrumVoiceId::Clave};
    }
  }

  static DrumHitParams hitParams(size_t hit, const DrumKitBytes& kit, const DrumParams& m) {
    using namespace Kit808;
    DrumHitParams p;
    p.pitchMul = drumPitchMul(m);
    p.decay = m.decay;
    switch (hit) {
      case kHitBD:
        p.gain = drumLevelGain(kitByte(kit, BassLevel));
        p.tone = kitKnob(kit, BassTone);
        p.decay = kitDecay(kit, BassDecay, m);
        break;
      case kHitSD:
        p.gain = drumLevelGain(kitByte(kit, SnareLevel));
        p.tone = kitKnob(kit, SnareTone);
        p.snappy = kitKnob(kit, SnareSnappy);
        break;
      case kHitLT:
        p.gain = drumLevelGain(kitByte(kit, LowTomLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, LowTomTuning));
        break;
      case kHitMT:
        p.gain = drumLevelGain(kitByte(kit, MidTomLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, MidTomTuning));
        break;
      case kHitHT:
        p.gain = drumLevelGain(kitByte(kit, HiTomLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, HiTomTuning));
        break;
      case kHitCH: p.gain = drumLevelGain(kitByte(kit, ClosedHatLevel)); break;
      case kHitOH:
        p.gain = drumLevelGain(kitByte(kit, OpenHatLevel));
        p.decay = kitDecay(kit, OpenHatDecay, m);
        break;
      case kHitCB: p.gain = drumLevelGain(kitByte(kit, CowBellLevel)); break;
      case kHitCP: p.gain = drumLevelGain(kitByte(kit, ClapLevel)); break;
      case kHitCY:
        p.gain = drumLevelGain(kitByte(kit, CymbalLevel));
        p.tone = kitKnob(kit, CymbalTone);
        p.decay = kitDecay(kit, CymbalDecay, m);
        break;
      case kHitRS:
      default:
        p.gain = drumLevelGain(kitByte(kit, RimShotLevel));
        break;
    }
    return p;
  }

  /** The 808 has no flam. */
  static float flamSeconds(const DrumKitBytes&) { return 0.0f; }
};

using Tr808Voice = DrumMachineVoice<Tr808Kit>;
extern template class DrumMachineVoice<Tr808Kit>;

} // namespace rb338
