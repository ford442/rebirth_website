#pragma once

#include "../parser/RbsTypes.h"
#include "../synth/SamplePool.h"
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
 *
 * `samplePool` holds decoded `.rbm` PCM. It is shared rather than owned so
 * that loading a new song keeps the current mod's samples alive, and so a
 * retired snapshot keeps its pool valid for as long as the audio thread
 * might still be reading through it. The audio thread only ever dereferences
 * the raw pointer the voices cached — it never touches the refcount.
 */
struct EngineSnapshot {
  ParsedSong song;
  std::array<std::unique_ptr<Voice>, NUM_DEVICES> voices;
  std::unique_ptr<Mixer> mixer;
  std::shared_ptr<const SamplePool> samplePool;
};

/**
 * Allocate and fully initialise a snapshot. Main thread only.
 *
 * `samplePool` may be null, in which case every voice stays procedural.
 */
std::unique_ptr<EngineSnapshot> buildEngineSnapshot(
    const ParsedSong& song, const EngineConfig& config,
    std::shared_ptr<const SamplePool> samplePool = nullptr);

/**
 * Re-apply the snapshot's song to its mixer and voices. Main thread only —
 * it allocates.
 *
 * TRAK automation mutates device knobs and mixer levels as a song plays, and
 * those changes live on in the snapshot. Anything that needs to replay from
 * a known starting state (an offline bounce, most importantly) has to undo
 * that first, or the second render inherits the first render's ending knobs.
 */
void applySongStateToSnapshot(EngineSnapshot& snap);

} // namespace rb338
