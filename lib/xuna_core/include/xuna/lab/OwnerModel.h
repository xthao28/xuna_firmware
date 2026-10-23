#pragma once

#include <cstdint>

#include <xuna/base/Random.h>

namespace xuna {

// Simulated owner for the closed-loop Life Lab (plan §9.9). Approval depends
// on the action XUNA actually showed, so learners can be compared on regret.
// Host/lab only: production never consumes simulated feedback.
enum class OwnerKind : uint8_t {
  Oracle = 0,   // §9.9 B: fixed preferred action per context
  Reversal,     // §9.9 C: preferred action switches at changeAt
  Drift,        // gradual: approval moves linearly over driftLength trials
  NoFeedback,   // §9.9 D: interactions, never a rating
  // Personality PE-0 owners (2026-10-23). Same fixed preferences as Oracle
  // unless noted; every probability is a Proposed lab value.
  Noisy,        // weak likes: noisyPreferredQ16 / noisyOtherQ16
  Sparse,       // reacts rarely: sparseRateQ16 instead of rateQ16
  Random,       // no real preference: every action randomQ16
  Teacher,      // always reacts, always consistent (1 / 0)
  Alternating,  // preferred / laterPreferred swap every `period` trials
};

struct OwnerConfig {
  static constexpr uint8_t kMaxContexts = 8;
  static constexpr uint8_t kMaxActions = 8;

  OwnerKind kind = OwnerKind::Oracle;
  uint8_t contexts = 2;
  uint8_t actions = 2;
  uint8_t preferred[kMaxContexts] = {};
  uint8_t laterPreferred[kMaxContexts] = {};
  uint16_t preferredQ16 = 58982;  // P(approve | preferred) = 0.9
  uint16_t otherQ16 = 13107;      // P(approve | other) = 0.2
  uint16_t rateQ16 = 65535;       // P(owner rates a shown action)
  uint32_t changeAt = 0;
  uint32_t driftLength = 1;
  uint16_t noisyPreferredQ16 = 39322;  // 0.6
  uint16_t noisyOtherQ16 = 26214;      // 0.4
  uint16_t sparseRateQ16 = 3277;       // 0.05
  uint16_t randomQ16 = 32768;          // 0.5
  uint32_t period = 1;                 // Alternating, in trials
};

class OwnerModel {
 public:
  explicit OwnerModel(const OwnerConfig& config = OwnerConfig())
      : config_(config) {
    if (config_.contexts == 0 || config_.contexts > OwnerConfig::kMaxContexts)
      config_.contexts = 1;
    if (config_.actions == 0 || config_.actions > OwnerConfig::kMaxActions)
      config_.actions = 1;
    if (config_.driftLength == 0) config_.driftLength = 1;
    if (config_.period == 0) config_.period = 1;
  }

  const OwnerConfig& config() const { return config_; }

  // True probability (Q16) that the owner approves `action` at `trial`.
  uint16_t approveQ16(uint32_t trial, uint8_t context, uint8_t action) const {
    if (context >= config_.contexts || action >= config_.actions) return 0;
    const uint8_t before = config_.preferred[context];
    const uint8_t after = config_.laterPreferred[context];
    switch (config_.kind) {
      case OwnerKind::Oracle:
      case OwnerKind::NoFeedback:
        return action == before ? config_.preferredQ16 : config_.otherQ16;
      case OwnerKind::Reversal: {
        const uint8_t target = trial < config_.changeAt ? before : after;
        return action == target ? config_.preferredQ16 : config_.otherQ16;
      }
      case OwnerKind::Sparse:
        return action == before ? config_.preferredQ16 : config_.otherQ16;
      case OwnerKind::Noisy:
        return action == before ? config_.noisyPreferredQ16
                                : config_.noisyOtherQ16;
      case OwnerKind::Random:
        return config_.randomQ16;
      case OwnerKind::Teacher:
        return action == before ? 65535U : 0;
      case OwnerKind::Alternating: {
        const uint8_t target = (trial / config_.period) % 2 ? after : before;
        return action == target ? config_.preferredQ16 : config_.otherQ16;
      }
      case OwnerKind::Drift: {
        const uint32_t progress = driftProgressQ16(trial);
        const uint32_t high = config_.preferredQ16;
        const uint32_t low = config_.otherQ16;
        const uint32_t delta = ((high - low) * progress) >> 16;
        if (action == before && action == after) return config_.preferredQ16;
        if (action == before) return static_cast<uint16_t>(high - delta);
        if (action == after) return static_cast<uint16_t>(low + delta);
        return config_.otherQ16;
      }
    }
    return 0;
  }

  // Action with the highest true approval; ties resolve to the lower index.
  uint8_t bestAction(uint32_t trial, uint8_t context) const {
    uint8_t best = 0;
    uint16_t bestQ16 = 0;
    for (uint8_t action = 0; action < config_.actions; ++action) {
      const uint16_t value = approveQ16(trial, context, action);
      if (action == 0 || value > bestQ16) {
        best = action;
        bestQ16 = value;
      }
    }
    return best;
  }

  bool rates(IRandom& random) const {
    if (config_.kind == OwnerKind::NoFeedback) return false;
    if (config_.kind == OwnerKind::Teacher) return true;
    if (config_.kind == OwnerKind::Sparse)
      return draw(random, config_.sparseRateQ16);
    return draw(random, config_.rateQ16);
  }

  bool approves(uint32_t trial, uint8_t context, uint8_t action,
                IRandom& random) const {
    return draw(random, approveQ16(trial, context, action));
  }

 private:
  uint32_t driftProgressQ16(uint32_t trial) const {
    if (trial <= config_.changeAt) return 0;
    const uint32_t elapsed = trial - config_.changeAt;
    if (elapsed >= config_.driftLength) return 65536U;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(elapsed) << 16) / config_.driftLength);
  }

  static bool draw(IRandom& random, uint32_t probabilityQ16) {
    if (probabilityQ16 >= 65535U) return true;
    return (random.next() >> 16) < probabilityQ16;
  }

  OwnerConfig config_;
};

}  // namespace xuna
