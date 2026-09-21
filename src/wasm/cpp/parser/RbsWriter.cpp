// RbsWriter — ParsedSong → `.rbs` bytes (ReBirth 2.x `CAT `/`RB40` container).
//
// Every layout constant here is the mirror of a read in RbsParser.cpp /
// RbsTrak.cpp; the two files are edited together. Where the on-disk field is
// one the parser does not decode (reserved bytes, loop points, the drum
// machines' kit state) the writer emits zeros: the round trip is semantic,
// not bitwise. RbsFormat.md §10 documents exactly what is dropped.

#include "RbsWriter.h"
#include "RbsByteStream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace rb338 {

namespace {

using detail::ByteSink;

// ── Chunk geometry (mirrors RbsParser.cpp) ───────────────────────────
constexpr size_t HEAD_SIZE = 256;
constexpr size_t GLOB_SIZE = 0x200;
constexpr size_t MIXR_SIZE = 64;
constexpr size_t MIXR_DEVICE_OFFSET = 0x10;
constexpr size_t MIXR_DEVICE_STRIDE = 12;
constexpr size_t DELY_SIZE = 8;
constexpr size_t PCF_SIZE = 12;
constexpr size_t DIST_SIZE = 8;
constexpr size_t COMP_SIZE = 8;
constexpr size_t TB303_HEADER_SIZE = 9;
constexpr size_t TR808_HEADER_SIZE = 30;
constexpr size_t TR909_HEADER_SIZE = 31;
constexpr size_t TB303_SLOT_SIZE = 34;
constexpr size_t DRUM_SLOT_SIZE = 194;
constexpr size_t PATTERN_SLOTS = 32;

// GLOB field offsets.
constexpr size_t GLOB_TITLE_OFFSET = 0x0f;
constexpr size_t GLOB_TITLE_WIDTH = 65;
constexpr size_t GLOB_FTP_URL_OFFSET = 0x50;
constexpr size_t GLOB_URL_WIDTH = 201;
constexpr size_t GLOB_WEB_URL_OFFSET = 0x119;

// TRAK track indices, in the order the nine chunks are written.
constexpr uint8_t TRACK_MIXER = 0;
constexpr uint8_t TRACK_FIRST_DEVICE = 1;
constexpr uint8_t PATTERN_SELECT_CONTROLLER = 0x01;

// ── Value conversion ─────────────────────────────────────────────────
uint8_t toMidiByte(float normalised) {
  const float scaled = std::round(normalised * 127.0f);
  if (!(scaled > 0.0f)) return 0; // also catches NaN
  return static_cast<uint8_t>(std::min(scaled, 127.0f));
}

/** Inverse of RbsParser's latin1ToUtf8: UTF-8 back to Latin-1 bytes. */
std::string utf8ToLatin1(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const uint8_t c = static_cast<uint8_t>(text[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if ((c == 0xc2 || c == 0xc3) && i + 1 < text.size()) {
      const uint8_t next = static_cast<uint8_t>(text[i + 1]);
      out.push_back(static_cast<char>(((c & 0x03) << 6) | (next & 0x3f)));
      ++i;
    } else {
      // Beyond Latin-1 (the parser can never produce it, but a caller can).
      out.push_back('?');
      while (i + 1 < text.size() &&
             (static_cast<uint8_t>(text[i + 1]) & 0xc0) == 0x80) {
        ++i;
      }
    }
  }
  return out;
}

/** Append one IFF chunk (id, big-endian size, body, alignment pad). */
void appendChunk(ByteSink& out, const char (&chunkId)[5],
                 const std::vector<uint8_t>& body) {
  out.id(chunkId);
  out.u32BE(static_cast<uint32_t>(body.size()));
  out.raw(body);
  out.padToEven();
}

/** Append a nested `CAT ` container whose body starts with a 4-byte marker. */
void appendNestedCat(ByteSink& out, const char (&marker)[5],
                     const std::vector<uint8_t>& chunks) {
  ByteSink body;
  body.id(marker);
  body.raw(chunks);
  appendChunk(out, "CAT ", body.bytes());
}

// ── Chunk bodies ─────────────────────────────────────────────────────
std::vector<uint8_t> buildHead(const ParsedSong& song) {
  ByteSink head;
  head.raw(reinterpret_cast<const uint8_t*>(RBS_HEAD_SIGNATURE), 4);
  head.padTo(0x06);
  // The parser maps this byte to RbsVersion; a v1/v1.5 source has no v2
  // marker of its own, so it is saved as 2.0.1.
  uint8_t headVersion = song.headVersion;
  if (headVersion != 0x01 && headVersion != 0x02) {
    headVersion = (song.version == RbsVersion::V2_0) ? 0x01 : 0x02;
  }
  head.u8(headVersion);
  head.padTo(HEAD_SIZE);
  return head.take();
}

std::vector<uint8_t> buildGlob(const ParsedSong& song) {
  ByteSink glob;
  // 0x00 play mode: song when there is an arrangement to play.
  glob.u8(song.arrangement.empty() ? 0 : 1);
  glob.u8(song.showInfoOnOpen ? 1 : 0);
  // 0x02 tempo ×1000. `globSubFormat` is byte 0x03, i.e. a byte *of* this
  // field — it is a read-side diagnostic, never written independently.
  glob.u32BE(static_cast<uint32_t>(std::lround(song.bpm * 1000.0f)));
  // 0x06 loop start, 0x0a loop end, 0x0e shuffle — not parsed, left zero.
  glob.padTo(GLOB_TITLE_OFFSET);
  glob.cStringField(song.title, GLOB_TITLE_WIDTH);
  glob.padTo(GLOB_FTP_URL_OFFSET);
  glob.cStringField(std::string(), GLOB_URL_WIDTH);
  glob.padTo(GLOB_WEB_URL_OFFSET);
  glob.cStringField(song.creatorUrl, GLOB_URL_WIDTH);
  glob.padTo(GLOB_SIZE);
  return glob.take();
}

std::vector<uint8_t> buildUsri(const ParsedSong& song) {
  ByteSink usri;
  usri.cString(utf8ToLatin1(song.author));
  usri.cString(utf8ToLatin1(song.infoText));
  // The parser needs at least 2 bytes; two empty strings already give that.
  return usri.take();
}

std::vector<uint8_t> buildMixr(const ParsedSong& song) {
  ByteSink mixr;
  mixr.u8(song.fx.masterLevel);
  mixr.padTo(MIXR_DEVICE_OFFSET);
  for (int i = 0; i < NUM_DEVICES; ++i) {
    const DeviceState& dev = song.devices[i];
    const size_t start = mixr.size();
    mixr.u8(dev.muted ? 0 : 1);
    mixr.u8(toMidiByte(dev.level));
    mixr.u8(toMidiByte(dev.pan));
    mixr.u8(toMidiByte(dev.delaySend));
    mixr.u8(dev.dist ? 1 : 0);
    mixr.u8(dev.pcf ? 1 : 0);
    mixr.u8(dev.compressor ? 1 : 0);
    mixr.padTo(start + MIXR_DEVICE_STRIDE);
  }
  mixr.padTo(MIXR_SIZE);
  return mixr.take();
}

std::vector<uint8_t> buildDely(const SongFxSettings& fx) {
  ByteSink sink;
  sink.u8(fx.delay.enabled ? 1 : 0);
  sink.u8(fx.delay.time);
  sink.u8(0); // observed byte 2 is not decoded
  sink.u8(fx.delay.feedback);
  sink.u8(fx.delay.wet);
  sink.padTo(DELY_SIZE);
  return sink.take();
}

std::vector<uint8_t> buildPcf(const SongFxSettings& fx) {
  ByteSink sink;
  sink.u8(fx.pcf.enabled ? 1 : 0);
  sink.u8(fx.pcf.cutoff);
  sink.u8(fx.pcf.resonance);
  sink.u8(fx.pcf.envAmount);
  sink.padTo(PCF_SIZE);
  return sink.take();
}

std::vector<uint8_t> buildDist(const SongFxSettings& fx) {
  ByteSink sink;
  sink.u8(fx.dist.enabled ? 1 : 0);
  sink.u8(fx.dist.drive);
  sink.u8(fx.dist.mix);
  sink.padTo(DIST_SIZE);
  return sink.take();
}

std::vector<uint8_t> buildComp(const SongFxSettings& fx) {
  ByteSink sink;
  sink.u8(fx.comp.enabled ? 1 : 0);
  sink.u8(fx.comp.ratio);
  sink.u8(fx.comp.threshold);
  sink.padTo(COMP_SIZE);
  return sink.take();
}

const Pattern* findPattern(const ParsedSong& song, DeviceId device, int slot) {
  const uint8_t bank = static_cast<uint8_t>(slot / MAX_PATTERNS_PER_BANK);
  const uint8_t index = static_cast<uint8_t>(slot % MAX_PATTERNS_PER_BANK);
  for (const Pattern& pattern : song.patterns) {
    if (pattern.deviceId == device && pattern.bank == bank &&
        pattern.patternIndex == index) {
      return &pattern;
    }
  }
  return nullptr;
}

void write303Slot(ByteSink& sink, const Pattern* pattern) {
  const size_t start = sink.size();
  sink.u8(0); // per-pattern shuffle flag
  sink.u8(pattern ? std::min<uint8_t>(pattern->length, MAX_STEPS) : MAX_STEPS);
  for (int step = 0; step < MAX_STEPS; ++step) {
    if (!pattern) {
      sink.u16BE(0);
      continue;
    }
    const StepData& data = pattern->steps[static_cast<size_t>(step)];
    sink.u8(data.active ? data.note : 0);
    sink.u8(static_cast<uint8_t>((data.accent ? 0x01 : 0) | (data.slide ? 0x02 : 0)));
  }
  sink.padTo(start + TB303_SLOT_SIZE);
}

void writeDrumSlot(ByteSink& sink, const Pattern* pattern) {
  const size_t start = sink.size();
  sink.u8(0);
  sink.u8(pattern ? std::min<uint8_t>(pattern->length, MAX_STEPS) : MAX_STEPS);
  for (int step = 0; step < MAX_STEPS; ++step) {
    if (!pattern) {
      sink.zeros(12);
      continue;
    }
    const StepData& data = pattern->steps[static_cast<size_t>(step)];
    const uint8_t hits = data.active ? data.note : 0;
    const uint8_t extra = data.active ? data.drumExtra : 0;
    const bool bassDrum = (hits & 0x01) != 0;
    sink.u8(bassDrum ? 1 : 0);
    // Byte 1 is the BD tweak/velocity the parser reads back as `accent`, and
    // it doubles as a BD indicator — so an accent is only meaningful on a
    // step that actually hits the bass drum.
    sink.u8((bassDrum && data.accent) ? 1 : 0);
    sink.u8((hits & 0x02) ? 1 : 0); // SD
    sink.u8((hits & 0x04) ? 1 : 0); // LT
    sink.u8((hits & 0x08) ? 1 : 0); // MT
    sink.u8((hits & 0x10) ? 1 : 0); // HT
    sink.u8((hits & 0x20) ? 1 : 0); // CH
    sink.u8((hits & 0x40) ? 1 : 0); // OH
    sink.u8((hits & 0x80) ? 1 : 0); // CL
    sink.u8((extra & 0x01) ? 1 : 0); // CP
    sink.u8((extra & 0x02) ? 1 : 0); // MA
    sink.u8((extra & 0x04) ? 1 : 0); // RS
  }
  sink.padTo(start + DRUM_SLOT_SIZE);
}

uint8_t selectedSlot(const DeviceState& dev) {
  return static_cast<uint8_t>(dev.initialPatternBank * MAX_PATTERNS_PER_BANK +
                              dev.initialPatternIndex);
}

std::vector<uint8_t> buildDeviceChunk(const ParsedSong& song, DeviceId device) {
  const bool is303 = (device == DeviceId::TB303_A || device == DeviceId::TB303_B);
  const size_t headerSize =
    is303 ? TB303_HEADER_SIZE
          : (device == DeviceId::TR808 ? TR808_HEADER_SIZE : TR909_HEADER_SIZE);

  const DeviceState& dev = song.devices[static_cast<int>(device)];
  ByteSink sink;
  sink.u8(dev.muted ? 0 : 1);
  sink.u8(selectedSlot(dev));
  if (is303) {
    sink.u8(toMidiByte(dev.tune));
    sink.u8(toMidiByte(dev.cutoff));
    sink.u8(toMidiByte(dev.resonance));
    sink.u8(toMidiByte(dev.envMod));
    sink.u8(toMidiByte(dev.decay));
    sink.u8(toMidiByte(dev.accent));
    sink.u8(dev.waveform ? 1 : 0);
  }
  // The drum machines' remaining kit state is not decoded by the parser.
  sink.padTo(headerSize);

  for (int slot = 0; slot < static_cast<int>(PATTERN_SLOTS); ++slot) {
    const Pattern* pattern = findPattern(song, device, slot);
    if (is303) {
      write303Slot(sink, pattern);
    } else {
      writeDrumSlot(sink, pattern);
    }
  }
  return sink.take();
}

// ── TRAK ─────────────────────────────────────────────────────────────
struct TrakEvent {
  uint32_t tick = 0;
  uint8_t controller = 0;
  uint8_t value = 0;
};

std::vector<uint8_t> buildTrak(const std::vector<TrakEvent>& events) {
  ByteSink sink;
  sink.u32BE(static_cast<uint32_t>(events.size()));
  uint32_t previous = 0;
  for (const TrakEvent& event : events) {
    sink.vlq(event.tick - previous);
    sink.u8(event.controller);
    sink.u8(event.value);
    previous = event.tick;
  }
  return sink.take();
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════

std::optional<std::vector<uint8_t>> RbsWriter::write(const ParsedSong& song) {
  m_error.clear();

  // ── Nine TRAK event lists, in chunk order ──
  std::array<std::vector<TrakEvent>, NUM_TRAK_TRACKS> tracks;

  // Pattern-select events (controller 0x01 on tracks 1-4) are projected back
  // out of `arrangement`, which is where the parser put them.
  if (!song.arrangement.empty()) {
    if (song.arrangement.size() > std::numeric_limits<uint16_t>::max()) {
      m_error = "Arrangement exceeds the supported bar count";
      return std::nullopt;
    }
    for (int device = 0; device < NUM_DEVICES; ++device) {
      auto& events = tracks[static_cast<size_t>(TRACK_FIRST_DEVICE + device)];
      int previousSlot = -1;
      for (size_t bar = 0; bar < song.arrangement.size(); ++bar) {
        const PatternRef& ref =
          song.arrangement[bar].devicePatterns[static_cast<size_t>(device)];
        if (ref.bank >= MAX_BANKS || ref.index >= MAX_PATTERNS_PER_BANK) {
          m_error = "Arrangement pattern reference exceeds the 32 pattern slots";
          return std::nullopt;
        }
        const int slot = ref.bank * MAX_PATTERNS_PER_BANK + ref.index;
        if (slot != previousSlot) {
          events.push_back(TrakEvent{
            static_cast<uint32_t>(bar) * detail::TRAK_TICKS_PER_BAR,
            PATTERN_SELECT_CONTROLLER, static_cast<uint8_t>(slot)});
          previousSlot = slot;
        }
      }
      // A terminal event on the final bar boundary pins the bar count the
      // parser derives from the highest TRAK position, so an arrangement
      // whose last bars repeat one pattern is not truncated on reload. It
      // sits past the last sampled bar, so it selects nothing.
      events.push_back(TrakEvent{
        static_cast<uint32_t>(song.arrangement.size()) * detail::TRAK_TICKS_PER_BAR,
        PATTERN_SELECT_CONTROLLER, static_cast<uint8_t>(previousSlot)});
    }
  }

  for (const AutomationEvent& event : song.automation) {
    if (event.trackIndex >= NUM_TRAK_TRACKS) {
      m_error = "Automation track index exceeds the nine TRAK tracks";
      return std::nullopt;
    }
    const bool deviceTrack = event.trackIndex >= TRACK_FIRST_DEVICE &&
                             event.trackIndex <= NUM_DEVICES;
    if (deviceTrack && event.controller == PATTERN_SELECT_CONTROLLER) {
      // Controller 1 on a device track is the pattern-select event the
      // parser routes into `arrangement`; it must not also be automation.
      m_error = "Automation event collides with the pattern-select controller";
      return std::nullopt;
    }
    tracks[event.trackIndex].push_back(
      TrakEvent{event.tickPosition, event.controller, event.value});
  }

  // TRAK deltas are unsigned, so events have to be emitted in tick order.
  // Stable, so automation keeps the order it was parsed in.
  for (auto& events : tracks) {
    std::stable_sort(events.begin(), events.end(),
                     [](const TrakEvent& a, const TrakEvent& b) {
                       return a.tick < b.tick;
                     });
  }

  // ── DEVL: mixer, FX, four device chunks ──
  ByteSink devl;
  appendChunk(devl, "MIXR", buildMixr(song));
  appendChunk(devl, "DELY", buildDely(song.fx));
  appendChunk(devl, "PCF ", buildPcf(song.fx));
  appendChunk(devl, "DIST", buildDist(song.fx));
  appendChunk(devl, "COMP", buildComp(song.fx));
  appendChunk(devl, "303 ", buildDeviceChunk(song, DeviceId::TB303_A));
  appendChunk(devl, "303 ", buildDeviceChunk(song, DeviceId::TB303_B));
  appendChunk(devl, "808 ", buildDeviceChunk(song, DeviceId::TR808));
  appendChunk(devl, "909 ", buildDeviceChunk(song, DeviceId::TR909));

  // ── TRKL: nine TRAK chunks ──
  ByteSink trkl;
  for (const auto& events : tracks) {
    appendChunk(trkl, "TRAK", buildTrak(events));
  }
  static_assert(TRACK_MIXER == 0, "TRAK chunk order starts at the mixer track");

  // ── Top-level chunk stream ──
  ByteSink body;
  appendChunk(body, "HEAD", buildHead(song));
  appendChunk(body, "GLOB", buildGlob(song));
  appendChunk(body, "USRI", buildUsri(song));
  appendNestedCat(body, "DEVL", devl.bytes());
  appendNestedCat(body, "TRKL", trkl.bytes());

  ByteSink file;
  file.id("CAT ");
  file.u32BE(static_cast<uint32_t>(4 + body.size())); // "RB40" + chunk stream
  file.id("RB40");
  file.raw(body.bytes());
  return file.take();
}

} // namespace rb338
