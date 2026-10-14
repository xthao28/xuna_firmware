#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/learning/PreferenceLearnerV2.h>

namespace xuna {

// A test/Life-Lab-only comparison harness. It keeps learner v1 as the
// baseline and feeds the same accepted feedback to learner v2. It has no
// production wiring and deliberately does not alter BLE or snapshot formats.
struct ShadowDecision {
  uint32_t id = 0;
  uint32_t decidedAt = 0;
  uint8_t context = 0;
  uint8_t baselineAction = 0;
  LearningDecision candidate;
};

struct ShadowMetrics {
  uint32_t decisions = 0;
  uint32_t agreements = 0;
  uint32_t candidateExploratory = 0;
  uint32_t feedbackAccepted = 0;
  uint32_t feedbackRejected = 0;
};

class LearningShadow {
 public:
  explicit LearningShadow(uint64_t seed = 1, uint64_t stream = 3)
      : random_(seed, stream) {}

  ShadowDecision choose(uint8_t context, uint32_t id, uint32_t now) {
    ShadowDecision result;
    result.id = id;
    result.decidedAt = now;
    result.context = context;
    result.baselineAction = baseline_.choose(context);
    result.candidate = candidate_.choose(context, id, now, random_);
    if (result.candidate.id == 0) return result;

    ++metrics_.decisions;
    if (result.baselineAction == result.candidate.action) ++metrics_.agreements;
    if (result.candidate.exploratory) ++metrics_.candidateExploratory;
    return result;
  }

  // Baseline feedback is applied only after candidate feedback succeeds. This
  // means a duplicate/invalid candidate feedback cannot double-train v1.
  bool feedback(const ShadowDecision& decision, bool approved, uint8_t source,
                uint32_t now) {
    if (source < 1 || source > 2) {
      ++metrics_.feedbackRejected;
      return false;
    }
    if (!candidate_.feedback(decision.candidate, approved, source, now)) {
      ++metrics_.feedbackRejected;
      return false;
    }
    baseline_.feedback(decision.context, decision.baselineAction, approved,
                       source);
    ++metrics_.feedbackAccepted;
    return true;
  }

  const LifeEngine& baseline() const { return baseline_; }
  const PreferenceLearnerV2& candidate() const { return candidate_; }
  const ShadowMetrics& metrics() const { return metrics_; }

  uint32_t fingerprint() const {
    uint8_t bytes[LifeEngine::kSnapshotSize] = {};
    baseline_.encode(bytes);
    uint32_t hash = 2166136261U;
    for (uint8_t byte : bytes) hash = (hash ^ byte) * 16777619U;
    const uint32_t candidateHash = candidate_.fingerprint();
    for (uint8_t i = 0; i < 4; ++i) {
      hash = (hash ^ ((candidateHash >> (8U * i)) & 255U)) * 16777619U;
    }
    return hash;
  }

 private:
  LifeEngine baseline_;
  PreferenceLearnerV2 candidate_;
  Pcg32 random_;
  ShadowMetrics metrics_;
};

}  // namespace xuna
