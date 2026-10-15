#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/learning/PreferenceLearnerV3.h>

namespace xuna {

// Learner section payload for LifeSnapshot v2 (plan §7.5). The sectioned
// container, CRC-32 trailer and storage medium belong to S12a/D2; this codec
// only owns the learner payload and its migrations. Little-endian layout:
//
//   0  u8  interactions      4  u32 policyVersion
//   1  u8  dayParts          8  cells[i][d][a]: u32 positive (Q16.16 mass),
//   2  u8  actions                              u32 negative, u16 rated
//   3  u8  flags (0)
//
// Only the configured shape is stored: 2 × 5 × 4 = 40 cells = 408 bytes.
enum class LearnerDecodeError : uint8_t {
  None = 0,
  BadSize,
  BadShape,
  BadFlags,
  BadValue,
};

struct LearnerDecodeResult {
  LearnerDecodeError error = LearnerDecodeError::None;
  uint16_t droppedCells = 0;  // stored cells outside the current shape
  bool ok() const { return error == LearnerDecodeError::None; }
};

class LearnerV3Codec {
 public:
  static constexpr uint8_t kSectionVersion = 1;
  static constexpr size_t kHeaderSize = 8;
  static constexpr size_t kCellSize = 10;
  // Sanity bound for stored masses: far above any γ ≥ 0.5 steady state.
  static constexpr uint32_t kMaxMassQ16 = 1024U << 16;

  static size_t encodedSize(const LearnerV3Config& config) {
    return kHeaderSize + kCellSize * size_t(config.interactions) *
                             config.dayParts * config.actions;
  }

  // Returns bytes written, or 0 if `capacity` is too small.
  static size_t encode(const PreferenceLearnerV3& learner, uint8_t* out,
                       size_t capacity) {
    const LearnerV3Config& c = learner.config_;
    const size_t size = encodedSize(c);
    if (out == 0 || capacity < size) return 0;
    out[0] = c.interactions;
    out[1] = c.dayParts;
    out[2] = c.actions;
    out[3] = 0;
    put32(out + 4, learner.policyVersion_);
    uint8_t* cursor = out + kHeaderSize;
    for (uint8_t i = 0; i < c.interactions; ++i)
      for (uint8_t d = 0; d < c.dayParts; ++d)
        for (uint8_t a = 0; a < c.actions; ++a) {
          const PreferenceLearnerV3::Cell& cell = learner.cells_[i][d][a];
          put32(cursor, cell.positive);
          put32(cursor + 4, cell.negative);
          put16(cursor + 8, cell.rated);
          cursor += kCellSize;
        }
    return size;
  }

  // Loads into `learner`, keeping its configuration. Stored cells inside the
  // configured shape are copied; cells outside it are counted and dropped, and
  // configured cells that were not stored stay empty (e.g. after D5 adds
  // actions). On any error the learner is left reset (fresh), never partial.
  static LearnerDecodeResult decode(const uint8_t* in, size_t size,
                                    PreferenceLearnerV3& learner) {
    LearnerDecodeResult result;
    learner.reset(learner.config_);
    if (in == 0 || size < kHeaderSize) {
      result.error = LearnerDecodeError::BadSize;
      return result;
    }
    const uint8_t interactions = in[0], dayParts = in[1], actions = in[2];
    if (interactions == 0 || dayParts == 0 || actions == 0 ||
        interactions > LearnerV3Config::kMaxInteractions ||
        dayParts > LearnerV3Config::kMaxDayParts ||
        actions > LearnerV3Config::kMaxActions) {
      result.error = LearnerDecodeError::BadShape;
      return result;
    }
    if (in[3] != 0) {
      result.error = LearnerDecodeError::BadFlags;
      return result;
    }
    const size_t expected = kHeaderSize + kCellSize * size_t(interactions) *
                                              dayParts * actions;
    if (size != expected) {
      result.error = LearnerDecodeError::BadSize;
      return result;
    }

    // Validate everything before mutating the learner.
    const uint8_t* cursor = in + kHeaderSize;
    for (size_t n = 0; n < size_t(interactions) * dayParts * actions; ++n) {
      if (get32(cursor) > kMaxMassQ16 || get32(cursor + 4) > kMaxMassQ16) {
        result.error = LearnerDecodeError::BadValue;
        return result;
      }
      cursor += kCellSize;
    }

    const LearnerV3Config& c = learner.config_;
    cursor = in + kHeaderSize;
    for (uint8_t i = 0; i < interactions; ++i)
      for (uint8_t d = 0; d < dayParts; ++d)
        for (uint8_t a = 0; a < actions; ++a) {
          if (i < c.interactions && d < c.dayParts && a < c.actions) {
            PreferenceLearnerV3::Cell& cell = learner.cells_[i][d][a];
            cell.positive = get32(cursor);
            cell.negative = get32(cursor + 4);
            cell.rated = get16(cursor + 8);
          } else if (get32(cursor) != 0 || get32(cursor + 4) != 0 ||
                     get16(cursor + 8) != 0) {
            ++result.droppedCells;
          }
          cursor += kCellSize;
        }
    learner.policyVersion_ = get32(in + 4) == 0 ? 1 : get32(in + 4);
    return result;
  }

  // Migrates v1 counts into coarse v3 cells (day part unknown). The ratio of
  // positive to negative evidence is kept exactly where possible, but the
  // total mass per cell is capped at the learner's effective memory
  // 1/(1 − γ), so a long v1 history cannot freeze the discounted learner.
  // Plan §7.5 originally said alpha = 1 + positive; the cap is a Proposed
  // amendment backed by the 2026-10-15 Life Lab reversal results.
  static bool migrateFromV1(const LifeEngine& v1, PreferenceLearnerV3& learner) {
    learner.reset(learner.config_);
    const LearnerV3Config& c = learner.config_;
    if (c.interactions < 2 || c.actions < 2) return false;
    const uint32_t cap = effectiveMemoryQ16(c.gammaQ16);
    for (uint8_t i = 0; i < 2; ++i)
      for (uint8_t a = 0; a < 2; ++a) {
        const uint32_t positive = v1.positive[i][a];
        const uint32_t negative = v1.negative[i][a];
        const uint32_t total = positive + negative;
        PreferenceLearnerV3::Cell& cell = learner.cells_[i][0][a];
        cell.rated = total > 0xFFFFU ? 0xFFFFU : static_cast<uint16_t>(total);
        if (total == 0) continue;
        const uint64_t totalQ16 = uint64_t(total) << 16;
        if (totalQ16 <= cap) {
          cell.positive = positive << 16;
          cell.negative = negative << 16;
        } else {
          cell.positive = static_cast<uint32_t>(
              (uint64_t(positive) * cap + total / 2U) / total);
          cell.negative = cap - cell.positive;
        }
      }
    return true;
  }

  // 1/(1 − γ) in Q16.16; γ = 1 has no steady state, so use the decode bound.
  static uint32_t effectiveMemoryQ16(uint32_t gammaQ16) {
    if (gammaQ16 >= 65536U) return kMaxMassQ16;
    const uint64_t memory = (uint64_t(65536) << 16) / (65536U - gammaQ16);
    return memory > kMaxMassQ16 ? kMaxMassQ16 : static_cast<uint32_t>(memory);
  }

 private:
  static void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
  static void put32(uint8_t* p, uint32_t v) {
    for (uint8_t i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8U * i));
  }
  static uint16_t get16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8));
  }
  static uint32_t get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
  }
};

}  // namespace xuna
