#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/learning/PreferenceLearnerV2.h>

namespace xuna {

// A bounded bridge for running the v2 learner against an existing v1 state.
// The bridge deliberately does not own either learner or change persistence
// bytes. It is the seam that LifeCore can use once its event/persistence
// contracts are ready.
struct LearningBridgeDecision {
  uint32_t id = 0;
  uint32_t decidedAt = 0;
  uint8_t context = 0;
  uint8_t baselineAction = 0;
  LearningDecision candidate;
};

struct LearningBridgeMetrics {
  uint32_t decisions = 0;
  uint32_t agreements = 0;
  uint32_t feedbackAccepted = 0;
  uint32_t feedbackRejected = 0;
};

class LearningBridge {
 public:
  LearningBridge(LifeEngine& baseline, PreferenceLearnerV2& candidate,
                 IRandom& random)
      : baseline_(baseline), candidate_(candidate), random_(random) {}

  LearningBridgeDecision choose(uint8_t context, uint32_t id,
                                uint32_t now) {
    LearningBridgeDecision result;
    result.id = id;
    result.decidedAt = now;
    result.context = context;
    result.baselineAction = baseline_.choose(context);
    result.candidate = candidate_.choose(context, id, now, random_);
    if (result.candidate.id == 0) return result;

    ++metrics_.decisions;
    if (result.baselineAction == result.candidate.action)
      ++metrics_.agreements;
    return result;
  }

  // Candidate feedback is committed first. This prevents a duplicate or
  // invalid v2 event from training the legacy v1 learner a second time.
  bool feedback(const LearningBridgeDecision& decision, bool approved,
                uint8_t source, uint32_t now) {
    if (source < 1 || source > 2 || decision.candidate.id == 0 ||
        decision.candidate.id != decision.id || decision.context > 1 ||
        decision.baselineAction > 1) {
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

  const LearningBridgeMetrics& metrics() const { return metrics_; }
  const LifeEngine& baseline() const { return baseline_; }
  const PreferenceLearnerV2& candidate() const { return candidate_; }

 private:
  LifeEngine& baseline_;
  PreferenceLearnerV2& candidate_;
  IRandom& random_;
  LearningBridgeMetrics metrics_;
};

}  // namespace xuna
