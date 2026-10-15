#pragma once

#include <cstdint>

#include <xuna/base/Random.h>

namespace xuna {

// Learner v3 candidate: plan §9.3 estimator and §9.4 policies (E10).
// Fixed-point discounted Beta with a hierarchical day-part prior. Only the
// rated cell is discounted, so silence and time never change estimates.
// Lab/shadow candidate only; production keeps v1 until E10 and persistence v2.
enum class ExplorePolicy : uint8_t {
  Greedy = 0,        // no exploration (isolates the estimator)
  EpsilonGreedy,     // §9.4 A: confidence-scaled ε
  Ucb,               // §9.4 C: optimism bonus
  LearningProgress,  // §9.4 D: ε follows |change| in prediction error
};

struct LearnerV3Config {
  static constexpr uint8_t kMaxInteractions = 4;
  static constexpr uint8_t kMaxDayParts = 5;  // 0 = unknown (coarse only)
  static constexpr uint8_t kMaxActions = 8;

  uint8_t interactions = 2;
  uint8_t dayParts = 5;
  uint8_t actions = 2;
  ExplorePolicy policy = ExplorePolicy::EpsilonGreedy;
  uint32_t gammaQ16 = 62259;              // γ = 0.95 [P]
  uint32_t priorStrengthQ16 = 2U << 16;   // k = 2 [P]
  uint32_t fineMinQ16 = 3U << 16;         // m = 3 [P]
  uint32_t epsilonMaxQ16 = 19661;         // 0.30 [P]
  uint32_t epsilonMinQ16 = 1966;          // 0.03 [P]
  uint32_t confidenceQ16 = 5U << 16;      // c = 5 [P]
  uint32_t ucbBonusQ16 = 16384;           // 0.25 [P]
  uint32_t progressGain = 4;              // ε_D = ε_min + gain·|Δerror| [P]
  // Scale ε by the recent share of decisions that received a rating, so
  // A/D stop exploring when nobody answers [P].
  bool feedbackAware = false;
  // Stated preferences (plan §11.6, D17): weight of one confirmed statement
  // and the cap on stated mass per cell, in ratings. Both [P] until S25.
  uint32_t statedWeightQ16 = 32768;  // w_s = 0.5
  uint32_t statedCapQ16 = 2U << 16;  // 2 explicit ratings
};

struct V3Decision {
  uint32_t id = 0;
  uint8_t interaction = 0;
  uint8_t dayPart = 0;
  uint8_t action = 0;
  bool explore = false;
  uint16_t predictedQ16 = 0;  // estimate of the shown action at choose time
  uint32_t policyVersion = 0;
};

struct V3CellView {
  uint16_t pQ16 = 0;
  uint32_t nEffQ16 = 0;
  uint32_t positiveQ16 = 0;  // discounted positive mass
  uint32_t negativeQ16 = 0;  // discounted negative mass
  uint16_t rated = 0;        // raw ratings in the cell, saturating
  bool fine = false;         // estimate taken from the day-part cell
  uint32_t statedPositiveQ16 = 0;  // part of positiveQ16 the owner stated
  uint32_t statedNegativeQ16 = 0;  // part of negativeQ16 the owner stated
};

class LearnerV3Codec;

class PreferenceLearnerV3 {
 public:
  static constexpr uint8_t kHistory = 8;

  explicit PreferenceLearnerV3(const LearnerV3Config& config = LearnerV3Config()) {
    reset(config);
  }

  void reset(const LearnerV3Config& config = LearnerV3Config()) {
    config_ = config;
    if (config_.interactions == 0 ||
        config_.interactions > LearnerV3Config::kMaxInteractions)
      config_.interactions = 1;
    if (config_.dayParts == 0 ||
        config_.dayParts > LearnerV3Config::kMaxDayParts)
      config_.dayParts = 1;
    if (config_.actions == 0 || config_.actions > LearnerV3Config::kMaxActions)
      config_.actions = 1;
    if (config_.gammaQ16 > 65536U) config_.gammaQ16 = 65536U;
    for (uint8_t i = 0; i < LearnerV3Config::kMaxInteractions; ++i) {
      fastErrorQ16_[i] = 16384;
      slowErrorQ16_[i] = 16384;
      for (uint8_t d = 0; d < LearnerV3Config::kMaxDayParts; ++d)
        for (uint8_t a = 0; a < LearnerV3Config::kMaxActions; ++a)
          cells_[i][d][a] = Cell();
    }
    for (uint8_t i = 0; i < kHistory; ++i) applied_[i] = 0;
    appliedNext_ = 0;
    policyVersion_ = 1;
    ratedShareQ16_ = 65536;
    lastRated_ = true;
  }

  const LearnerV3Config& config() const { return config_; }

  // `allowedMask` bit a = action a is allowed (FSM, cooldown, energy, stage).
  V3Decision choose(uint8_t interaction, uint8_t dayPart, uint32_t id,
                    uint8_t allowedMask, IRandom& random) {
    V3Decision decision;
    decision.id = id;
    decision.interaction = interaction;
    decision.dayPart = dayPart;
    decision.policyVersion = policyVersion_;
    allowedMask = static_cast<uint8_t>(allowedMask & actionMask());
    if (id == 0 || !validContext(interaction, dayPart) || allowedMask == 0)
      return V3Decision();

    // Transient exploration state, not an estimate: tracks whether the
    // previous decision was rated. Excluded from the fingerprint.
    const int32_t target = lastRated_ ? 65536 : 0;
    ratedShareQ16_ = static_cast<uint32_t>(
        static_cast<int32_t>(ratedShareQ16_) +
        (target - static_cast<int32_t>(ratedShareQ16_)) / 8);
    lastRated_ = false;

    const uint8_t greedyAction =
        bestBy(interaction, dayPart, allowedMask, false, random);
    uint8_t chosen = greedyAction;
    switch (config_.policy) {
      case ExplorePolicy::Greedy:
        break;
      case ExplorePolicy::Ucb:
        chosen = bestBy(interaction, dayPart, allowedMask, true, random);
        decision.explore = chosen != greedyAction;
        break;
      case ExplorePolicy::EpsilonGreedy:
      case ExplorePolicy::LearningProgress: {
        const uint8_t others =
            static_cast<uint8_t>(allowedMask & ~(1U << greedyAction));
        if (others != 0 &&
            (random.next() >> 16) < epsilonQ16(interaction, dayPart)) {
          chosen = leastRated(interaction, dayPart, others, random);
          decision.explore = true;
        }
        break;
      }
    }
    decision.action = chosen;
    decision.predictedQ16 = view(interaction, dayPart, chosen).pQ16;
    return decision;
  }

  // True when feedback() would apply `decision`; no side effects. Lets a
  // caller commit durable state first and apply afterwards (plan §9.5).
  bool accepts(const V3Decision& decision) const {
    if (decision.id == 0 ||
        !validContext(decision.interaction, decision.dayPart) ||
        decision.action >= config_.actions)
      return false;
    for (uint8_t i = 0; i < kHistory; ++i)
      if (applied_[i] == decision.id) return false;
    return true;
  }

  // Explicit feedback for a shown decision. Duplicates within the last eight
  // applied IDs and invalid decisions are rejected without side effects.
  bool feedback(const V3Decision& decision, bool approved) {
    if (!accepts(decision)) return false;

    trackError(decision.interaction, decision.predictedQ16, approved);
    rate(cells_[decision.interaction][0][decision.action], approved);
    if (decision.dayPart != 0)
      rate(cells_[decision.interaction][decision.dayPart][decision.action],
           approved);
    lastRated_ = true;
    applied_[appliedNext_] = decision.id;
    appliedNext_ = static_cast<uint8_t>((appliedNext_ + 1U) % kHistory);
    ++policyVersion_;
    return true;
  }

  // A confirmed stated preference (plan §11.6, opcode 6): `user_stated`
  // evidence of weight w_s on the coarse cell and, with a day part, on the
  // fine cell, capped per cell. It is not a rating: `rated`, the duplicate
  // window and the error trackers are unchanged.
  bool state(uint8_t interaction, uint8_t dayPart, uint8_t action, bool like) {
    if (!validContext(interaction, dayPart) || action >= config_.actions)
      return false;
    stateInto(cells_[interaction][0][action], like);
    if (dayPart != 0) stateInto(cells_[interaction][dayPart][action], like);
    ++policyVersion_;
    return true;
  }

  // Estimate used for decisions: fine cell when it has nEff ≥ m, else coarse.
  V3CellView view(uint8_t interaction, uint8_t dayPart, uint8_t action) const {
    V3CellView result;
    if (!validContext(interaction, dayPart) || action >= config_.actions)
      return result;
    const Cell& coarse = cells_[interaction][0][action];
    const uint32_t coarseP = meanQ16(65536U + coarse.positive,
                                     65536U + coarse.negative);
    result.pQ16 = clampQ16(coarseP);
    result.nEffQ16 = coarse.positive + coarse.negative;
    result.positiveQ16 = coarse.positive;
    result.negativeQ16 = coarse.negative;
    result.rated = coarse.rated;
    result.statedPositiveQ16 = uint32_t(coarse.statedPositive) << 8;
    result.statedNegativeQ16 = uint32_t(coarse.statedNegative) << 8;
    if (dayPart == 0) return result;

    const Cell& fine = cells_[interaction][dayPart][action];
    const uint32_t fineN = fine.positive + fine.negative;
    if (fineN < config_.fineMinQ16) return result;
    const uint32_t alpha0 = static_cast<uint32_t>(
        (uint64_t(config_.priorStrengthQ16) * coarseP) >> 16);
    const uint32_t beta0 = config_.priorStrengthQ16 - alpha0;
    result.pQ16 = clampQ16(
        meanQ16(alpha0 + fine.positive, beta0 + fine.negative));
    result.nEffQ16 = fineN;
    result.positiveQ16 = fine.positive;
    result.negativeQ16 = fine.negative;
    result.rated = fine.rated;
    result.statedPositiveQ16 = uint32_t(fine.statedPositive) << 8;
    result.statedNegativeQ16 = uint32_t(fine.statedNegative) << 8;
    result.fine = true;
    return result;
  }

  uint8_t greedy(uint8_t interaction, uint8_t dayPart) const {
    uint8_t best = 0;
    uint16_t bestP = 0;
    for (uint8_t a = 0; a < config_.actions; ++a) {
      const uint16_t p = view(interaction, dayPart, a).pQ16;
      if (a == 0 || p > bestP) {
        best = a;
        bestP = p;
      }
    }
    return best;
  }

  uint32_t epsilonQ16(uint8_t interaction, uint8_t dayPart) const {
    if (!validContext(interaction, dayPart)) return 0;
    uint64_t nEff = 0;
    for (uint8_t a = 0; a < config_.actions; ++a)
      nEff += view(interaction, dayPart, a).nEffQ16;
    uint32_t epsilon = 0;
    if (config_.policy == ExplorePolicy::EpsilonGreedy) {
      const uint64_t c = config_.confidenceQ16;
      epsilon = static_cast<uint32_t>((config_.epsilonMaxQ16 * c) / (c + nEff));
    } else if (config_.policy == ExplorePolicy::LearningProgress) {
      if (nEff < config_.confidenceQ16) {
        epsilon = config_.epsilonMaxQ16;  // novelty: too little evidence yet
      } else {
        const int32_t delta = static_cast<int32_t>(slowErrorQ16_[interaction]) -
                              static_cast<int32_t>(fastErrorQ16_[interaction]);
        const uint32_t progress =
            static_cast<uint32_t>(delta < 0 ? -delta : delta);
        epsilon = config_.epsilonMinQ16 + progress * config_.progressGain;
      }
    } else {
      return 0;
    }
    if (epsilon > config_.epsilonMaxQ16) epsilon = config_.epsilonMaxQ16;
    if (epsilon < config_.epsilonMinQ16) epsilon = config_.epsilonMinQ16;
    if (config_.feedbackAware)
      epsilon = static_cast<uint32_t>((uint64_t(epsilon) * ratedShareQ16_) >> 16);
    return epsilon;
  }

  uint32_t policyVersion() const { return policyVersion_; }

  uint32_t fingerprint() const {
    uint32_t hash = 2166136261U;
    mix(hash, config_.interactions);
    mix(hash, config_.dayParts);
    mix(hash, config_.actions);
    for (uint8_t i = 0; i < config_.interactions; ++i) {
      for (uint8_t d = 0; d < config_.dayParts; ++d)
        for (uint8_t a = 0; a < config_.actions; ++a) {
          mix32(hash, cells_[i][d][a].positive);
          mix32(hash, cells_[i][d][a].negative);
          mix32(hash, cells_[i][d][a].rated);
          mix32(hash, cells_[i][d][a].statedPositive);
          mix32(hash, cells_[i][d][a].statedNegative);
        }
    }
    // Error EMAs (variant D) and the rated share are transient exploration
    // state: not persisted and not fingerprinted.
    return hash;
  }

 private:
  friend class LearnerV3Codec;

  struct Cell {
    uint32_t positive = 0;  // discounted evidence mass above the prior, Q16
    uint32_t negative = 0;
    uint16_t rated = 0;     // raw rating count, saturating
    // Share of positive/negative that came from stated preferences, Q8.8
    // (≤ statedCapQ16; always ≤ the mass it is part of).
    uint16_t statedPositive = 0;
    uint16_t statedNegative = 0;
  };

  bool validContext(uint8_t interaction, uint8_t dayPart) const {
    return interaction < config_.interactions && dayPart < config_.dayParts;
  }

  uint8_t actionMask() const {
    return static_cast<uint8_t>(config_.actions >= 8
                                    ? 0xFFU
                                    : (1U << config_.actions) - 1U);
  }

  void rate(Cell& cell, bool approved) const {
    cell.positive = discount(cell.positive) + (approved ? 65536U : 0U);
    cell.negative = discount(cell.negative) + (approved ? 0U : 65536U);
    if (cell.rated < 0xFFFFU) ++cell.rated;
    // Stated evidence ages with the cell like any other evidence.
    cell.statedPositive = clampStated(discount(uint32_t(cell.statedPositive) << 8),
                                      cell.positive);
    cell.statedNegative = clampStated(discount(uint32_t(cell.statedNegative) << 8),
                                      cell.negative);
  }

  // Latest statement wins: the opposite stated mass is withdrawn (explicit
  // ratings are untouched), then w_s is added up to the per-cell cap.
  void stateInto(Cell& cell, bool like) const {
    uint32_t& same = like ? cell.positive : cell.negative;
    uint32_t& opposite = like ? cell.negative : cell.positive;
    uint16_t& sameStated = like ? cell.statedPositive : cell.statedNegative;
    uint16_t& oppositeStated = like ? cell.statedNegative : cell.statedPositive;
    const uint32_t withdrawn = uint32_t(oppositeStated) << 8;
    opposite = opposite > withdrawn ? opposite - withdrawn : 0;
    oppositeStated = 0;
    const uint32_t current = uint32_t(sameStated) << 8;
    const uint32_t room =
        config_.statedCapQ16 > current ? config_.statedCapQ16 - current : 0;
    const uint32_t add =
        config_.statedWeightQ16 < room ? config_.statedWeightQ16 : room;
    same += add;
    sameStated = clampStated(current + add, same);
  }

  // Q16 → Q8.8, never above the mass it belongs to.
  static uint16_t clampStated(uint32_t statedQ16, uint32_t massQ16) {
    const uint32_t value = statedQ16 < massQ16 ? statedQ16 : massQ16;
    const uint32_t q8 = (value + 128U) >> 8;
    const uint32_t limit = massQ16 >> 8;
    const uint32_t bounded = q8 > limit ? limit : q8;
    return static_cast<uint16_t>(bounded > 0xFFFFU ? 0xFFFFU : bounded);
  }

  uint32_t discount(uint32_t value) const {
    return static_cast<uint32_t>(
        (uint64_t(value) * config_.gammaQ16 + 32768U) >> 16);
  }

  void trackError(uint8_t interaction, uint16_t predictedQ16, bool approved) {
    const int32_t outcome = approved ? 65536 : 0;
    const int32_t diff = static_cast<int32_t>(predictedQ16) - outcome;
    const int32_t error = static_cast<int32_t>(
        (static_cast<int64_t>(diff) * diff) >> 16);
    int32_t fast = static_cast<int32_t>(fastErrorQ16_[interaction]);
    int32_t slow = static_cast<int32_t>(slowErrorQ16_[interaction]);
    fast += (error - fast) / 4;
    slow += (error - slow) / 16;
    fastErrorQ16_[interaction] = static_cast<uint32_t>(fast < 0 ? 0 : fast);
    slowErrorQ16_[interaction] = static_cast<uint32_t>(slow < 0 ? 0 : slow);
  }

  static uint32_t meanQ16(uint32_t alpha, uint32_t beta) {
    const uint64_t total = uint64_t(alpha) + beta;
    return total == 0 ? 32768U
                      : static_cast<uint32_t>((uint64_t(alpha) << 16) / total);
  }

  static uint16_t clampQ16(uint32_t value) {
    return value > 65535U ? 65535U : static_cast<uint16_t>(value);
  }

  static uint32_t isqrt(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = uint64_t(1) << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
      if (value >= result + bit) {
        value -= result + bit;
        result = (result >> 1) + bit;
      } else {
        result >>= 1;
      }
      bit >>= 2;
    }
    return static_cast<uint32_t>(result);
  }

  // Highest estimate (or UCB score) among allowed actions; ties by RNG.
  uint8_t bestBy(uint8_t interaction, uint8_t dayPart, uint8_t allowed,
                 bool optimistic, IRandom& random) const {
    uint32_t scores[LearnerV3Config::kMaxActions] = {};
    uint32_t best = 0;
    uint8_t ties = 0;
    for (uint8_t a = 0; a < config_.actions; ++a) {
      if (!(allowed & (1U << a))) continue;
      const V3CellView cell = view(interaction, dayPart, a);
      uint32_t score = cell.pQ16;
      if (optimistic)
        score += static_cast<uint32_t>(
            (uint64_t(config_.ucbBonusQ16) << 8) /
            isqrt(uint64_t(cell.nEffQ16) + 65536U));
      scores[a] = score;
      if (ties == 0 || score > best) {
        best = score;
        ties = 1;
      } else if (score == best) {
        ++ties;
      }
    }
    return pick(scores, allowed, best, ties, random);
  }

  // Least-rated allowed action (lowest nEff); ties by RNG.
  uint8_t leastRated(uint8_t interaction, uint8_t dayPart, uint8_t allowed,
                     IRandom& random) const {
    uint32_t scores[LearnerV3Config::kMaxActions] = {};
    uint32_t best = 0;
    uint8_t ties = 0;
    for (uint8_t a = 0; a < config_.actions; ++a) {
      if (!(allowed & (1U << a))) continue;
      // Invert so that "highest score" means "least evidence".
      const uint32_t score = 0xFFFFFFFFU - view(interaction, dayPart, a).nEffQ16;
      scores[a] = score;
      if (ties == 0 || score > best) {
        best = score;
        ties = 1;
      } else if (score == best) {
        ++ties;
      }
    }
    return pick(scores, allowed, best, ties, random);
  }

  uint8_t pick(const uint32_t* scores, uint8_t allowed, uint32_t best,
               uint8_t ties, IRandom& random) const {
    const uint32_t rank = ties > 1 ? random.below(ties) : 0;
    uint32_t seen = 0;
    for (uint8_t a = 0; a < config_.actions; ++a) {
      if (!(allowed & (1U << a)) || scores[a] != best) continue;
      if (seen++ == rank) return a;
    }
    return 0;
  }

  static void mix(uint32_t& hash, uint8_t value) {
    hash = (hash ^ value) * 16777619U;
  }
  static void mix32(uint32_t& hash, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) mix(hash, static_cast<uint8_t>(value >> (8U * i)));
  }

  LearnerV3Config config_;
  Cell cells_[LearnerV3Config::kMaxInteractions][LearnerV3Config::kMaxDayParts]
             [LearnerV3Config::kMaxActions];
  uint32_t fastErrorQ16_[LearnerV3Config::kMaxInteractions] = {};
  uint32_t slowErrorQ16_[LearnerV3Config::kMaxInteractions] = {};
  uint32_t applied_[kHistory] = {};
  uint8_t appliedNext_ = 0;
  uint32_t policyVersion_ = 1;
  uint32_t ratedShareQ16_ = 65536;
  bool lastRated_ = true;
};

}  // namespace xuna
