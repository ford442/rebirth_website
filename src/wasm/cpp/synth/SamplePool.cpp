#include "SamplePool.h"

namespace rb338 {

const char* modLoadStatusName(ModLoadStatus status) {
  switch (status) {
    case ModLoadStatus::Ok: return "ok";
    case ModLoadStatus::NotInitialised: return "not-initialised";
    case ModLoadStatus::NoSamples: return "no-samples";
    case ModLoadStatus::ArenaExhausted: return "arena-exhausted";
    case ModLoadStatus::Partial: return "partial";
  }
  return "unknown";
}

ModLoadReport summariseMod(const ParsedMod& mod) {
  ModLoadReport report;
  report.title = mod.title;
  report.description = mod.description;
  report.copyright = mod.copyright;
  report.capacityFrames = static_cast<uint32_t>(SamplePool::kDefaultArenaFrames);
  report.resources.reserve(mod.resources.size());

  size_t totalFrames = 0;
  uint32_t decodable = 0;

  for (const auto& resource : mod.resources) {
    ModSampleReportEntry entry;
    entry.name = resource.name;
    entry.kind = resource.kind;
    entry.slot = resource.slot;
    entry.byteSize = static_cast<uint32_t>(resource.bytes.size());

    if (resource.kind == ModResourceKind::Skin) {
      ++report.skinCount;
    }

    if (resource.kind == ModResourceKind::Sample && resource.slot != ModSampleSlot::Unknown) {
      SampleInfo info;
      entry.decodeStatus = probeSample(resource.bytes.data(), resource.bytes.size(), info);
      if (entry.decodeStatus == SampleDecodeStatus::Ok) {
        entry.frameCount = info.frameCount;
        entry.sampleRate = info.sampleRate;
        entry.channels = info.channels;
        entry.bitDepth = info.bitDepth;
        entry.loaded = true; // "would load"
        totalFrames += info.frameCount;
        ++decodable;
      }
    }

    report.resources.push_back(std::move(entry));
  }

  report.loadedSlots = decodable;
  report.usedFrames = static_cast<uint32_t>(
      totalFrames > 0xffffffffull ? 0xffffffffull : totalFrames);
  if (decodable == 0) {
    report.status = ModLoadStatus::NoSamples;
  } else if (totalFrames > SamplePool::kDefaultArenaFrames) {
    report.status = ModLoadStatus::ArenaExhausted;
  } else {
    report.status = ModLoadStatus::Ok;
  }
  return report;
}

bool SamplePool::init(size_t arenaFrames) {
  if (arenaFrames == 0) return false;

  // The one and only allocation. Everything after this point writes into
  // the arena in place, so slot pointers never dangle.
  m_arena.assign(arenaFrames, 0.0f);
  if (m_arena.size() != arenaFrames) return false;

  m_slots.fill(SlotData{});
  m_used = 0;
  m_loadedSlots = 0;
  m_initialised = true;
  return true;
}

void SamplePool::clear() {
  m_slots.fill(SlotData{});
  m_used = 0;
  m_loadedSlots = 0;
}

bool SamplePool::hasSlot(ModSampleSlot slot) const {
  const auto index = static_cast<size_t>(slot);
  if (index >= m_slots.size()) return false;
  return m_slots[index].pcm != nullptr && m_slots[index].frameCount > 0;
}

const SamplePool::SlotData* SamplePool::slotData(ModSampleSlot slot) const {
  const auto index = static_cast<size_t>(slot);
  if (index >= m_slots.size()) return nullptr;
  const SlotData& data = m_slots[index];
  if (data.pcm == nullptr || data.frameCount == 0) return nullptr;
  return &data;
}

ModLoadStatus SamplePool::loadFromParsedMod(const ParsedMod& mod, ModLoadReport& report) {
  report = ModLoadReport{};
  report.title = mod.title;
  report.description = mod.description;
  report.copyright = mod.copyright;
  report.capacityFrames = static_cast<uint32_t>(m_arena.size());

  if (!m_initialised) {
    report.status = ModLoadStatus::NotInitialised;
    return report.status;
  }

  clear();

  bool anyLoaded = false;
  bool anyFailed = false;
  bool exhausted = false;

  report.resources.reserve(mod.resources.size());

  for (const auto& resource : mod.resources) {
    ModSampleReportEntry entry;
    entry.name = resource.name;
    entry.kind = resource.kind;
    entry.slot = resource.slot;
    entry.byteSize = static_cast<uint32_t>(resource.bytes.size());

    if (resource.kind == ModResourceKind::Skin) {
      ++report.skinCount;
    }

    // Only sample payloads with a known slot are decoded. Skins and bundled
    // songs stay as catalogue entries — decoding a JPEG here would put image
    // data in the audio heap for no reason.
    if (resource.kind != ModResourceKind::Sample || resource.slot == ModSampleSlot::Unknown) {
      report.resources.push_back(std::move(entry));
      continue;
    }

    SampleInfo info;
    const SampleDecodeStatus probeStatus =
        probeSample(resource.bytes.data(), resource.bytes.size(), info);
    if (probeStatus != SampleDecodeStatus::Ok) {
      entry.decodeStatus = probeStatus;
      anyFailed = true;
      report.resources.push_back(std::move(entry));
      continue;
    }

    entry.sampleRate = info.sampleRate;
    entry.channels = info.channels;
    entry.bitDepth = info.bitDepth;

    const size_t remaining = m_arena.size() - m_used;
    if (info.frameCount > remaining) {
      entry.decodeStatus = SampleDecodeStatus::DestinationTooSmall;
      exhausted = true;
      anyFailed = true;
      report.resources.push_back(std::move(entry));
      continue;
    }

    float* dest = m_arena.data() + m_used;
    SampleInfo decoded;
    const SampleDecodeStatus status =
        decodeSampleMono(resource.bytes.data(), resource.bytes.size(), dest, remaining, decoded);
    entry.decodeStatus = status;

    if (status != SampleDecodeStatus::Ok || decoded.frameCount == 0) {
      anyFailed = true;
      report.resources.push_back(std::move(entry));
      continue;
    }

    const auto slotIndex = static_cast<size_t>(resource.slot);
    if (slotIndex >= m_slots.size()) {
      anyFailed = true;
      report.resources.push_back(std::move(entry));
      continue;
    }

    // A later duplicate of the same slot replaces the earlier one; the
    // earlier frames stay allocated but unreferenced until the next clear().
    if (m_slots[slotIndex].pcm == nullptr) {
      ++m_loadedSlots;
    }
    m_slots[slotIndex] = SlotData{dest, decoded.frameCount,
                                  decoded.sampleRate > 0 ? decoded.sampleRate : 44100u};
    m_used += decoded.frameCount;

    entry.frameCount = decoded.frameCount;
    entry.sampleRate = m_slots[slotIndex].sampleRate;
    entry.channels = decoded.channels;
    entry.bitDepth = decoded.bitDepth;
    entry.loaded = true;
    anyLoaded = true;
    report.resources.push_back(std::move(entry));
  }

  report.loadedSlots = m_loadedSlots;
  report.usedFrames = static_cast<uint32_t>(m_used);

  if (!anyLoaded) {
    report.status = exhausted ? ModLoadStatus::ArenaExhausted : ModLoadStatus::NoSamples;
  } else if (exhausted) {
    report.status = ModLoadStatus::ArenaExhausted;
  } else if (anyFailed) {
    report.status = ModLoadStatus::Partial;
  } else {
    report.status = ModLoadStatus::Ok;
  }
  return report.status;
}

} // namespace rb338
