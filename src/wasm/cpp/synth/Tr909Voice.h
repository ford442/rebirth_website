#pragma once

#include "DrumMachineVoice.h"

namespace rb338 {

/**
 * TR-909 kit. The CB / CY bits are the crash and ride; the two hats share a
 * level knob and have their own decays, and the closed hat chokes the open.
 */
struct Tr909Kit {
  static constexpr DeviceId kDevice = DeviceId::TR909;
  static constexpr bool kIs909 = true;

  enum Channel : uint8_t {
    Kick, Snare, ClosedHat, OpenHat, Clap, Rimshot, Crash, Ride,
    LowTom, MidTom, HighTom, Count
  };
  static constexpr uint8_t kClosedHat = ClosedHat;
  static constexpr uint8_t kOpenHat = OpenHat;

  static constexpr std::array<ModSampleSlot, Count> kSlots = {
      ModSampleSlot::Tr909Kick,     ModSampleSlot::Tr909Snare,
      ModSampleSlot::Tr909ClosedHat, ModSampleSlot::Tr909OpenHat,
      ModSampleSlot::Tr909Clap,     ModSampleSlot::Tr909Rimshot,
      ModSampleSlot::Tr909Crash,    ModSampleSlot::Tr909Ride,
      ModSampleSlot::Tr909LowTom,   ModSampleSlot::Tr909MidTom,
      ModSampleSlot::Tr909HighTom,
  };

  static constexpr std::array<DrumRoute, kNumDrumHits> kRoutes = {{
      {Kick, DrumVoiceId::Kick},           // BD
      {Snare, DrumVoiceId::Snare},         // SD
      {LowTom, DrumVoiceId::LowTom},       // LT
      {MidTom, DrumVoiceId::MidTom},       // MT
      {HighTom, DrumVoiceId::HighTom},     // HT
      {ClosedHat, DrumVoiceId::ClosedHat}, // CH
      {OpenHat, DrumVoiceId::OpenHat},     // OH
      {Crash, DrumVoiceId::Crash},         // CB bit = crash
      {Clap, DrumVoiceId::Clap},           // CP
      {Ride, DrumVoiceId::Ride},           // CY bit = ride
      {Rimshot, DrumVoiceId::Rimshot},     // RS
  }};

  static DrumRoute route(size_t hit, const DrumKitBytes&) {
    return kRoutes[std::min<size_t>(hit, kNumDrumHits - 1)];
  }

  static DrumHitParams hitParams(size_t hit, const DrumKitBytes& kit, const DrumParams& m) {
    using namespace Kit909;
    DrumHitParams p;
    p.pitchMul = drumPitchMul(m);
    p.decay = m.decay;
    switch (hit) {
      case kHitBD:
        p.gain = drumLevelGain(kitByte(kit, BassLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, BassTune));
        p.attack = kitKnob(kit, BassAttack);
        p.decay = kitDecay(kit, BassDecay, m);
        break;
      case kHitSD:
        p.gain = drumLevelGain(kitByte(kit, SnareLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, SnareTune));
        p.tone = kitKnob(kit, SnareTone);
        p.snappy = kitKnob(kit, SnareSnappy);
        break;
      case kHitLT:
        p.gain = drumLevelGain(kitByte(kit, LowTomLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, LowTomTune));
        p.decay = kitDecay(kit, LowTomDecay, m);
        break;
      case kHitMT:
        p.gain = drumLevelGain(kitByte(kit, MidTomLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, MidTomTune));
        p.decay = kitDecay(kit, MidTomDecay, m);
        break;
      case kHitHT:
        p.gain = drumLevelGain(kitByte(kit, HiTomLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, HiTomTune));
        p.decay = kitDecay(kit, HiTomDecay, m);
        break;
      case kHitCH:
        p.gain = drumLevelGain(kitByte(kit, HiHatLevel));
        p.decay = kitDecay(kit, ClosedHatDecay, m);
        break;
      case kHitOH:
        p.gain = drumLevelGain(kitByte(kit, HiHatLevel));
        p.decay = kitDecay(kit, OpenHatDecay, m);
        break;
      case kHitCB:
        p.gain = drumLevelGain(kitByte(kit, CrashLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, CrashTune));
        break;
      case kHitCP: p.gain = drumLevelGain(kitByte(kit, ClapLevel)); break;
      case kHitCY:
        p.gain = drumLevelGain(kitByte(kit, RideLevel));
        p.pitchMul *= drumTuneMul(kitByte(kit, RideTune));
        break;
      case kHitRS:
      default:
        p.gain = drumLevelGain(kitByte(kit, RimShotLevel));
        break;
    }
    return p;
  }

  /**
   * FLAM INTERVAL knob → grace-to-hit spacing, 2–40 ms. ReBirth's exact
   * range is not documented; this brackets a musical flam and wants a
   * golden render to calibrate.
   */
  static float flamSeconds(const DrumKitBytes& kit) {
    return 0.002f + 0.038f * kitKnob(kit, Kit909::FlamInterval);
  }
};

using Tr909Voice = DrumMachineVoice<Tr909Kit>;
extern template class DrumMachineVoice<Tr909Kit>;

} // namespace rb338
