#pragma once

#include <cstddef>
#include <cstdint>
#include <xuna/base/Random.h>

namespace xuna {

// Bounded contextual preference learning for the device. This is deliberately
// not a neural network: it updates one small posterior per context/action pair
// and can run in the single firmware loop with no heap allocation.
struct LearningConfig {
  uint8_t contextCount = 2;
  uint8_t actionCount = 2;
  uint16_t priorPositive = 1;
  uint16_t priorNegative = 1;
  uint16_t explorationQ16 = 16384;  // 0.25 optimism bonus at first sample.
  uint16_t maxEvidence = 1000;
};

struct LearningDecision {
  uint32_t id = 0;
  uint32_t decidedAt = 0;
  uint8_t context = 0;
  uint8_t action = 0;
  uint16_t propensityQ16 = 0;
  bool exploratory = false;
  uint32_t policyVersion = 0;
};

struct LearningRecord {
  uint32_t decisionId = 0;
  uint32_t decidedAt = 0;
  uint32_t feedbackAt = 0;
  uint32_t policyVersion = 0;
  uint8_t context = 0;
  uint8_t action = 0;
  uint8_t source = 0;
  bool approved = false;
};

struct LearningPosterior {
  uint16_t positive = 0;
  uint16_t negative = 0;
  uint16_t meanQ16 = 0;
  uint16_t count = 0;
};

struct LearningMetrics {
  uint32_t decisions = 0;
  uint32_t exploratoryDecisions = 0;
  uint32_t feedback = 0;
  uint32_t approved = 0;
  uint32_t rejected = 0;
  uint32_t duplicateFeedback = 0;
  uint32_t invalidFeedback = 0;
  uint32_t policyVersion = 1;
};

class PreferenceLearnerV2 {
 public:
  static constexpr uint8_t kMaxContexts = 8;
  static constexpr uint8_t kMaxActions = 8;
  static constexpr uint8_t kHistorySize = 8;

  explicit PreferenceLearnerV2(const LearningConfig& config = LearningConfig())
      : config_(config) {
    reset(config);
  }

  void reset(const LearningConfig& config = LearningConfig()) {
    config_ = config;
    if (config_.contextCount == 0 || config_.contextCount > kMaxContexts)
      config_.contextCount = 1;
    if (config_.actionCount == 0 || config_.actionCount > kMaxActions)
      config_.actionCount = 1;
    if (config_.maxEvidence == 0) config_.maxEvidence = 1;
    for (uint8_t context = 0; context < kMaxContexts; ++context) {
      for (uint8_t action = 0; action < kMaxActions; ++action) {
        cells_[context][action].positive = 0;
        cells_[context][action].negative = 0;
      }
    }
    for (uint8_t i = 0; i < kHistorySize; ++i) history_[i] = LearningRecord();
    historyNext_ = 0;
    historyCount_ = 0;
    metrics_ = LearningMetrics();
  }

  LearningDecision choose(uint8_t context, uint32_t decisionId,
                          uint32_t now, IRandom& random) {
    LearningDecision decision;
    decision.id = decisionId;
    decision.decidedAt = now;
    decision.context = context;
    decision.policyVersion = metrics_.policyVersion;
    if (decisionId == 0 || context >= config_.contextCount) return decision;

    uint32_t bestScore = 0;
    uint8_t bestCount = 0;
    for (uint8_t action = 0; action < config_.actionCount; ++action) {
      const uint32_t score = optimisticScore(context, action);
      if (bestCount == 0 || score > bestScore) {
        bestScore = score;
        bestCount = 1;
      } else if (score == bestScore) {
        ++bestCount;
      }
    }
    if (bestCount == 0) return decision;

    const uint8_t selectedRank =
        static_cast<uint8_t>(random.below(bestCount));
    uint8_t rank = 0;
    for (uint8_t action = 0; action < config_.actionCount; ++action) {
      if (optimisticScore(context, action) != bestScore) continue;
      if (rank++ != selectedRank) continue;
      decision.action = action;
      decision.propensityQ16 =
          static_cast<uint16_t>(65535U / bestCount);
      decision.exploratory = bestCount > 1 ||
                             posterior(context, action).count == 0;
      ++metrics_.decisions;
      if (decision.exploratory) ++metrics_.exploratoryDecisions;
      return decision;
    }
    return LearningDecision();
  }

  // Feedback is tied to the exact decision ID. Replaying the same feedback
  // cannot update the posterior twice, which makes retries safe.
  bool feedback(const LearningDecision& decision, bool approved, uint8_t source,
                uint32_t now) {
    if (!validDecision(decision) || source == 0) {
      ++metrics_.invalidFeedback;
      return false;
    }
    if (hasDecision(decision.id)) {
      ++metrics_.duplicateFeedback;
      return false;
    }
    Cell& cell = cells_[decision.context][decision.action];
    if (cell.positive >= config_.maxEvidence ||
        cell.negative >= config_.maxEvidence) {
      cell.positive = static_cast<uint16_t>((cell.positive + 1U) / 2U);
      cell.negative = static_cast<uint16_t>((cell.negative + 1U) / 2U);
    }
    if (approved) {
      ++cell.positive;
      ++metrics_.approved;
    } else {
      ++cell.negative;
      ++metrics_.rejected;
    }
    LearningRecord& record = history_[historyNext_];
    record.decisionId = decision.id;
    record.decidedAt = decision.decidedAt;
    record.feedbackAt = now;
    record.policyVersion = decision.policyVersion;
    record.context = decision.context;
    record.action = decision.action;
    record.source = source;
    record.approved = approved;
    historyNext_ = static_cast<uint8_t>((historyNext_ + 1U) % kHistorySize);
    if (historyCount_ < kHistorySize) ++historyCount_;
    ++metrics_.feedback;
    ++metrics_.policyVersion;
    return true;
  }

  LearningPosterior posterior(uint8_t context, uint8_t action) const {
    LearningPosterior result;
    if (context >= config_.contextCount || action >= config_.actionCount)
      return result;
    const Cell& cell = cells_[context][action];
    result.positive = cell.positive;
    result.negative = cell.negative;
    const uint32_t positive = uint32_t(cell.positive) + config_.priorPositive;
    const uint32_t negative = uint32_t(cell.negative) + config_.priorNegative;
    const uint32_t total = positive + negative;
    result.meanQ16 = total == 0
                         ? 0
                         : static_cast<uint16_t>((uint64_t(positive) << 16) / total);
    const uint32_t count = uint32_t(cell.positive) + cell.negative;
    result.count = count > 65535U ? 65535U : static_cast<uint16_t>(count);
    return result;
  }

  const LearningConfig& config() const { return config_; }
  const LearningMetrics& metrics() const { return metrics_; }
  uint8_t historyCount() const { return historyCount_; }
  const LearningRecord& history(uint8_t index) const {
    static const LearningRecord empty;
    if (index >= historyCount_) return empty;
    const uint8_t oldest = historyCount_ == kHistorySize ? historyNext_ : 0;
    return history_[(oldest + index) % kHistorySize];
  }

  // Stable, small fingerprint for golden traces; this is not a cryptographic
  // checksum and is intentionally independent of platform word size.
  uint32_t fingerprint() const {
    uint32_t hash = 2166136261U;
    hashByte(hash, config_.contextCount);
    hashByte(hash, config_.actionCount);
    for (uint8_t context = 0; context < config_.contextCount; ++context) {
      for (uint8_t action = 0; action < config_.actionCount; ++action) {
        hashWord(hash, cells_[context][action].positive);
        hashWord(hash, cells_[context][action].negative);
      }
    }
    for (uint8_t i = 0; i < historyCount_; ++i) {
      const LearningRecord& record = history(i);
      hashWord(hash, record.decisionId);
      hashWord(hash, record.decidedAt);
      hashWord(hash, record.feedbackAt);
      hashByte(hash, record.context);
      hashByte(hash, record.action);
      hashByte(hash, record.source);
      hashByte(hash, record.approved ? 1 : 0);
    }
    return hash;
  }

 private:
  struct Cell {
    uint16_t positive = 0;
    uint16_t negative = 0;
  };

  static uint16_t integerSqrt(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1U << 30;
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
    return result > 65535U ? 65535U : static_cast<uint16_t>(result);
  }

  uint32_t optimisticScore(uint8_t context, uint8_t action) const {
    const LearningPosterior belief = posterior(context, action);
    const uint32_t denominator =
        uint32_t(integerSqrt(uint32_t(belief.count) + 1U));
    const uint32_t bonus = denominator == 0
                               ? config_.explorationQ16
                               : config_.explorationQ16 / denominator;
    return uint32_t(belief.meanQ16) + bonus;
  }

  bool validDecision(const LearningDecision& decision) const {
    return decision.id != 0 && decision.context < config_.contextCount &&
           decision.action < config_.actionCount;
  }

  bool hasDecision(uint32_t id) const {
    for (uint8_t i = 0; i < historyCount_; ++i) {
      if (history(i).decisionId == id) return true;
    }
    return false;
  }

  static void hashByte(uint32_t& hash, uint8_t value) {
    hash = (hash ^ value) * 16777619U;
  }

  static void hashWord(uint32_t& hash, uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) hashByte(hash, (value >> (8U * i)) & 255U);
  }

  LearningConfig config_;
  Cell cells_[kMaxContexts][kMaxActions] = {};
  LearningRecord history_[kHistorySize] = {};
  uint8_t historyNext_ = 0;
  uint8_t historyCount_ = 0;
  LearningMetrics metrics_;
};

}  // namespace xuna
