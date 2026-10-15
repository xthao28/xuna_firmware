#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Crc32.h>
#include <xuna/learning/LearnerV3Codec.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/Needs.h>

namespace xuna {

// LifeSnapshot v2 (plan §7.5, layout frozen in S12a). Medium-independent:
// the same bytes go to NVS blobs, files or raw A/B slots (D2 decides, S13
// wires). Little-endian. Header and trailer are a permanent contract so any
// future reader can tell "newer" from "broken":
//
//   0  'X' 'U'           magic (v1 used the same magic with schema 1)
//   2  u8  schema = 2    > 2 → TooNew
//   3  u8  flags = 0     unknown bits → TooNew
//   4  u16 size          total bytes including the CRC trailer
//   6  u16 reserved = 0
//   8  u32 generation    A/B ordering, newest valid wins
//  12  sections          {u8 id, u8 version, u16 length, data[length]}
//  end u32 CRC-32 of every preceding byte
//
// Section id bit 7 = required: a reader that does not understand the
// section (unknown id or newer version) must return TooNew instead of
// dropping it. Unknown optional sections are skipped and their state loads
// defaults. Each section appears at most once.
enum class LoadResult : uint8_t {
  Ok = 0,
  Missing,
  Corrupt,
  TooNew,
  Migrated,
  MigrationFailed,
};

// Stable section ids (low 7 bits). Reserved ids have no code yet; they are
// listed so their numbers are never reused.
enum class SectionId : uint8_t {
  Counters = 1,
  Clock = 2,             // trusted-time watermark (S10)
  Lifecycle = 3,         // reserved: M06 stage
  Needs = 4,
  Personality = 5,       // reserved: M06
  Learner = 6,
  Habit = 7,             // reserved: M05d
  Skills = 8,            // reserved: teachable tricks
  Dedup = 9,             // reserved: durable command idempotency
  Pointers = 10,         // reserved: log/memory consistency
  Episodes = 11,         // v1 episodes until the M05b memory store
  StatedPreference = 12  // stated share of learner mass (S24a, optional)
};

constexpr uint8_t kSectionRequired = 0x80;

// Plan §8.3: rollback detection only. Time itself stays unknown after a boot
// until the next sync; the watermark never reconstructs offline time.
struct SnapshotClock {
  uint32_t lastTrustedUtcMinute = 0;  // 2020-epoch minutes; 0 = never synced
  int16_t tzOffsetMin = 0;
};

struct SnapshotCounters {
  uint32_t bootCounter = 0;        // copy of key `bootc` (consistency check)
  uint32_t activeSeconds = 0;
  uint32_t eventSeqCommitted = 0;  // 0 until the event log is persisted
  uint32_t feedbackCount = 0;      // v1 eventCount: applied feedback events
};

// Bounded episode ring carried over from v1 (BLE memory pages).
struct SnapshotEpisodes {
  static constexpr uint8_t kCapacity = 8;
  LifeEngine::Episode items[kCapacity];
  uint8_t count = 0;
  uint8_t next = 0;
};

struct LifeSnapshotV2 {
  uint32_t generation = 0;
  SnapshotCounters counters;
  SnapshotClock clock;    // written only once a sync was accepted
  bool hasNeeds = false;  // false → caller applies its cold-boot needs policy
  NeedsState needs;
  PreferenceLearnerV3 learner;  // its config sets the shape loaded into RAM
  SnapshotEpisodes episodes;

  explicit LifeSnapshotV2(const LearnerV3Config& config = productLearnerConfig())
      : learner(config) {}
};

struct SnapshotDecodeInfo {
  LoadResult result = LoadResult::Corrupt;
  uint32_t generation = 0;
  uint16_t droppedLearnerCells = 0;  // stored cells outside the RAM shape
  uint8_t skippedSections = 0;       // unknown optional sections
};

class SnapshotCodec {
 public:
  static constexpr uint8_t kSchemaVersion = 2;
  static constexpr size_t kBudget = 1536;  // per slot, plan §7.5
  static constexpr size_t kHeaderSize = 12;
  static constexpr size_t kSectionHeaderSize = 4;
  static constexpr size_t kTrailerSize = 4;
  static constexpr size_t kCountersSize = 16;
  static constexpr size_t kNeedsSize = 4;
  static constexpr size_t kClockSize = 6;
  static constexpr uint8_t kClockVersion = 1;
  static constexpr int16_t kMaxTzOffsetMin = 840;
  static constexpr size_t kEpisodesSize = 2 + 4 * SnapshotEpisodes::kCapacity;
  static constexpr uint8_t kCountersVersion = 1;
  static constexpr uint8_t kNeedsVersion = 1;
  static constexpr uint8_t kEpisodesVersion = 1;

  static size_t encodedSize(const LifeSnapshotV2& s) {
    return kHeaderSize + kSectionHeaderSize + kCountersSize +
           (s.clock.lastTrustedUtcMinute ? kSectionHeaderSize + kClockSize : 0) +
           (s.hasNeeds ? kSectionHeaderSize + kNeedsSize : 0) +
           kSectionHeaderSize + LearnerV3Codec::encodedSize(s.learner.config()) +
           statedSectionSize(s) +
           kSectionHeaderSize + kEpisodesSize + kTrailerSize;
  }

  // Returns bytes written, or 0 when `capacity` or the budget is too small.
  static size_t encode(const LifeSnapshotV2& s, uint8_t* out, size_t capacity) {
    const size_t size = encodedSize(s);
    if (out == 0 || size > capacity || size > kBudget) return 0;
    out[0] = 'X';
    out[1] = 'U';
    out[2] = kSchemaVersion;
    out[3] = 0;
    put16(out + 4, static_cast<uint16_t>(size));
    put16(out + 6, 0);
    put32(out + 8, s.generation);
    uint8_t* p = out + kHeaderSize;

    p = section(p, SectionId::Counters, true, kCountersVersion, kCountersSize);
    put32(p, s.counters.bootCounter);
    put32(p + 4, s.counters.activeSeconds);
    put32(p + 8, s.counters.eventSeqCommitted);
    put32(p + 12, s.counters.feedbackCount);
    p += kCountersSize;

    if (s.clock.lastTrustedUtcMinute) {
      p = section(p, SectionId::Clock, false, kClockVersion, kClockSize);
      put32(p, s.clock.lastTrustedUtcMinute);
      put16(p + 4, static_cast<uint16_t>(s.clock.tzOffsetMin));
      p += kClockSize;
    }

    if (s.hasNeeds) {
      p = section(p, SectionId::Needs, false, kNeedsVersion, kNeedsSize);
      put16(p, s.needs.energy);
      put16(p + 2, s.needs.stimulation);
      p += kNeedsSize;
    }

    const size_t learnerSize = LearnerV3Codec::encodedSize(s.learner.config());
    p = section(p, SectionId::Learner, true, LearnerV3Codec::kSectionVersion,
                static_cast<uint16_t>(learnerSize));
    if (LearnerV3Codec::encode(s.learner, p, learnerSize) != learnerSize)
      return 0;
    p += learnerSize;

    const size_t statedSize = LearnerV3Codec::statedSize(s.learner);
    if (statedSize != 0) {
      p = section(p, SectionId::StatedPreference, false,
                  LearnerV3Codec::kStatedSectionVersion,
                  static_cast<uint16_t>(statedSize));
      if (LearnerV3Codec::encodeStated(s.learner, p, statedSize) != statedSize)
        return 0;
      p += statedSize;
    }

    p = section(p, SectionId::Episodes, false, kEpisodesVersion, kEpisodesSize);
    p[0] = s.episodes.count;
    p[1] = s.episodes.next;
    for (uint8_t i = 0; i < SnapshotEpisodes::kCapacity; ++i) {
      const LifeEngine::Episode& e = s.episodes.items[i];
      p[2 + i * 4] = e.context;
      p[3 + i * 4] = e.action;
      p[4 + i * 4] = e.approved;
      p[5 + i * 4] = e.source;
    }
    p += kEpisodesSize;

    put32(p, crc32(out, size - kTrailerSize));
    return size;
  }

  // Validates everything before touching `out`; on any result other than Ok
  // `out` is unchanged. The learner keeps `out`'s configuration (shape and
  // parameters); stored cells are copied into that shape.
  static SnapshotDecodeInfo decode(const uint8_t* in, size_t size,
                                   LifeSnapshotV2& out) {
    SnapshotDecodeInfo info;
    LifeSnapshotV2 candidate(out.learner.config());
    info.result = parse(in, size, candidate, info);
    if (info.result == LoadResult::Ok) out = candidate;
    return info;
  }

  static uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
  }
  static uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
  }
  static void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
  static void put32(uint8_t* p, uint32_t v) {
    for (uint8_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8U * i));
  }

 private:
  static uint8_t* section(uint8_t* p, SectionId id, bool required,
                          uint8_t version, uint16_t length) {
    p[0] = static_cast<uint8_t>(static_cast<uint8_t>(id) |
                                (required ? kSectionRequired : 0));
    p[1] = version;
    put16(p + 2, length);
    return p + kSectionHeaderSize;
  }

  static LoadResult parse(const uint8_t* in, size_t size, LifeSnapshotV2& s,
                          SnapshotDecodeInfo& info) {
    // Header integrity first: a torn or foreign blob is Corrupt, only an
    // intact newer one is TooNew.
    if (in == 0 || size < kHeaderSize + kTrailerSize || size > kBudget ||
        in[0] != 'X' || in[1] != 'U' || get16(in + 4) != size ||
        get32(in + size - kTrailerSize) != crc32(in, size - kTrailerSize))
      return LoadResult::Corrupt;
    if (in[2] > kSchemaVersion || in[3] != 0) return LoadResult::TooNew;
    if (in[2] != kSchemaVersion || get16(in + 6) != 0) return LoadResult::Corrupt;
    info.generation = get32(in + 8);
    s.generation = info.generation;

    uint32_t seen[4] = {0, 0, 0, 0};  // one bit per 7-bit section id
    // The stated section refines learner cells, so it is applied after the
    // loop: decoding the learner section resets the learner.
    const uint8_t* stated = 0;
    uint16_t statedLength = 0;
    const uint8_t* p = in + kHeaderSize;
    const uint8_t* end = in + size - kTrailerSize;
    while (p < end) {
      if (size_t(end - p) < kSectionHeaderSize) return LoadResult::Corrupt;
      const uint8_t id = p[0] & 0x7F;
      const bool required = (p[0] & kSectionRequired) != 0;
      const uint8_t version = p[1];
      const uint16_t length = get16(p + 2);
      const uint8_t* data = p + kSectionHeaderSize;
      if (size_t(end - data) < length || id == 0 || version == 0)
        return LoadResult::Corrupt;
      p = data + length;
      const uint32_t bit = 1U << (id % 32);
      if (seen[id / 32] & bit) return LoadResult::Corrupt;
      seen[id / 32] |= bit;
      if (id == uint8_t(SectionId::StatedPreference) &&
          version <= LearnerV3Codec::kStatedSectionVersion) {
        stated = data;
        statedLength = length;
        continue;
      }
      const LoadResult r = parseSection(id, version, data, length, s, info);
      if (r == LoadResult::Missing) {  // not understood by this reader
        if (required) return LoadResult::TooNew;
        ++info.skippedSections;
      } else if (r != LoadResult::Ok) {
        return r;
      }
    }
    // Schema 2 writers always store counters and the learner.
    const uint32_t mandatory = (1U << uint8_t(SectionId::Counters)) |
                               (1U << uint8_t(SectionId::Learner));
    if ((seen[0] & mandatory) != mandatory) return LoadResult::Corrupt;
    if (stated != 0) {
      uint16_t dropped = 0;
      if (!LearnerV3Codec::decodeStated(stated, statedLength, s.learner, dropped))
        return LoadResult::Corrupt;
      info.droppedLearnerCells = static_cast<uint16_t>(info.droppedLearnerCells + dropped);
    }
    return LoadResult::Ok;
  }

  static size_t statedSectionSize(const LifeSnapshotV2& s) {
    const size_t size = LearnerV3Codec::statedSize(s.learner);
    return size == 0 ? 0 : kSectionHeaderSize + size;
  }

  // Ok = parsed, Missing = not understood (unknown id or newer version),
  // Corrupt = understood but invalid.
  static LoadResult parseSection(uint8_t id, uint8_t version,
                                 const uint8_t* d, uint16_t length,
                                 LifeSnapshotV2& s, SnapshotDecodeInfo& info) {
    switch (static_cast<SectionId>(id)) {
      case SectionId::Counters:
        if (version > kCountersVersion) return LoadResult::Missing;
        if (length != kCountersSize) return LoadResult::Corrupt;
        s.counters.bootCounter = get32(d);
        s.counters.activeSeconds = get32(d + 4);
        s.counters.eventSeqCommitted = get32(d + 8);
        s.counters.feedbackCount = get32(d + 12);
        return LoadResult::Ok;
      case SectionId::Clock: {
        if (version > kClockVersion) return LoadResult::Missing;
        if (length != kClockSize) return LoadResult::Corrupt;
        const int16_t tz = static_cast<int16_t>(get16(d + 4));
        if (get32(d) == 0 || tz < -kMaxTzOffsetMin || tz > kMaxTzOffsetMin)
          return LoadResult::Corrupt;
        s.clock.lastTrustedUtcMinute = get32(d);
        s.clock.tzOffsetMin = tz;
        return LoadResult::Ok;
      }
      case SectionId::Needs:
        if (version > kNeedsVersion) return LoadResult::Missing;
        if (length != kNeedsSize) return LoadResult::Corrupt;
        s.hasNeeds = true;
        s.needs.energy = get16(d);
        s.needs.stimulation = get16(d + 2);
        return LoadResult::Ok;
      case SectionId::Learner: {
        if (version > LearnerV3Codec::kSectionVersion) return LoadResult::Missing;
        const LearnerDecodeResult r = LearnerV3Codec::decode(d, length, s.learner);
        if (!r.ok()) return LoadResult::Corrupt;
        info.droppedLearnerCells = r.droppedCells;
        return LoadResult::Ok;
      }
      case SectionId::Episodes:
        if (version > kEpisodesVersion) return LoadResult::Missing;
        return parseEpisodes(d, length, s.episodes);
      default:
        return LoadResult::Missing;
    }
  }

  static LoadResult parseEpisodes(const uint8_t* d, uint16_t length,
                                  SnapshotEpisodes& e) {
    const uint8_t capacity = SnapshotEpisodes::kCapacity;
    if (length != kEpisodesSize || d[0] > capacity || d[1] >= capacity)
      return LoadResult::Corrupt;
    e.count = d[0];
    e.next = d[1];
    for (uint8_t i = 0; i < capacity; ++i) {
      LifeEngine::Episode& item = e.items[i];
      item.context = d[2 + i * 4];
      item.action = d[3 + i * 4];
      item.approved = d[4 + i * 4];
      item.source = d[5 + i * 4];
      if (item.context >= kLearnInteractionCount ||
          item.action >= kLearnActionCount || item.approved > 1 ||
          item.source > 2)
        return LoadResult::Corrupt;
    }
    const uint8_t oldest = e.count == capacity ? e.next : 0;
    for (uint8_t i = 0; i < e.count; ++i)
      if (e.items[(oldest + i) % capacity].source < 1) return LoadResult::Corrupt;
    return LoadResult::Ok;
  }
};

}  // namespace xuna
