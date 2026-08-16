#pragma once

#include "../parser/RbsTypes.h"
#include "../synth/Voice.h"
#include "Mixer.h"
#include <array>
#include <memory>

namespace rb338 {

struct EngineConfig;

/**
 * Immutable-from-the-audio-thread render graph.
 *
 * Built entirely on the main thread (voices loaded, mixer routed), then
 * published via an atomic pointer. The audio thread never constructs, loads,
 * or deletes one of these.
 */
struct EngineSnapshot {
  ParsedSong song;
  std::array<std::unique_ptr<Voice>, NUM_DEVICES> voices;
  std::unique_ptr<Mixer> mixer;
};

/** Allocate and fully initialise a snapshot. Main thread only. */
std::unique_ptr<EngineSnapshot> buildEngineSnapshot(const ParsedSong& song,
                                                    const EngineConfig& config);

} // namespace rb338
