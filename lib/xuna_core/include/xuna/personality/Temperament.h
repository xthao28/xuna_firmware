#pragma once

#include <cstdint>

#include <xuna/base/Random.h>

namespace xuna {

// Personality PE-1a (plan docs/roadmap/personality, 2026-10-23): per-pet
// temperament genome. Lab only until gate G1; nothing on the board reads it.
//
// Each trait is Q1.14 (16384 = 1.0). `baseline` is innate: drawn once from
// the durable IdentityRecord seed (±0.15) and never changed afterwards.
// `offset` is what life taught, moved only by EvidenceLedger consolidation
// (±0.6). Behaviour reads effective = clamp(baseline + offset, ±1).
// Every range and the trait list itself are Proposed.
enum class TraitId : uint8_t {
  Curiosity = 0,  // explore drive rate, UCB bonus scale
  Sociability,    // social drive rate, greeting warmth
  Playfulness,    // play drive rate
  Tempo,          // expression speed (frame period, reaction length)
  Sensitivity,    // emotion gain from owner feedback
  MoodInertia,    // mood spring mass: how slowly mood follows emotion
  MoodBaseline,   // mood prevalence: where mood settles
  Persistence,    // selection threshold: how long it sticks to an activity
  Boredom,        // boredom rise when nothing new happens
  Restfulness,    // rest drive rate
};

constexpr uint8_t kTraitCount = 10;
constexpr int16_t kTraitOne = 16384;           // 1.0 in Q1.14
constexpr int16_t kBaselineRange = 2458;       // ±0.15 [P]
constexpr int16_t kOffsetRange = 9830;         // ±0.6 [P]

struct TemperamentState {
  int16_t baseline[kTraitCount] = {};
  int16_t offset[kTraitCount] = {};
};

class Temperament {
 public:
  // Innate baseline from the pet's durable seed (IdentityRecord::seed, made
  // with the PetId at hatch or migration). Same seed, same temperament on
  // every platform. Offsets start at 0: nothing is invented about the past.
  static void seed(TemperamentState& state, uint32_t petSeed) {
    Pcg32 random(petSeed, kSeedStream);
    for (uint8_t i = 0; i < kTraitCount; ++i) {
      state.baseline[i] = static_cast<int16_t>(
          int32_t(random.below(2U * kBaselineRange + 1)) - kBaselineRange);
      state.offset[i] = 0;
    }
  }

  static int16_t effective(const TemperamentState& state, TraitId trait) {
    const uint8_t i = static_cast<uint8_t>(trait);
    if (i >= kTraitCount) return 0;
    return clamp(int32_t(state.baseline[i]) + state.offset[i], kTraitOne);
  }

  // Learned change only; the result stays within ±kOffsetRange. Returns the
  // delta actually applied (0 at the limit).
  static int16_t shift(TemperamentState& state, TraitId trait, int16_t delta) {
    const uint8_t i = static_cast<uint8_t>(trait);
    if (i >= kTraitCount) return 0;
    const int16_t before = state.offset[i];
    state.offset[i] = clamp(int32_t(before) + delta, kOffsetRange);
    return static_cast<int16_t>(state.offset[i] - before);
  }

  // ForgetAll: the pet keeps who it was born as, forgets what it became.
  static void forget(TemperamentState& state) {
    for (uint8_t i = 0; i < kTraitCount; ++i) state.offset[i] = 0;
  }

 private:
  static constexpr uint64_t kSeedStream = 0x7e3a;  // temperament stream

  static int16_t clamp(int32_t value, int16_t range) {
    if (value > range) return range;
    if (value < -range) return static_cast<int16_t>(-range);
    return static_cast<int16_t>(value);
  }
};

}  // namespace xuna
