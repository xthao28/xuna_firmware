#pragma once

#include <cstdint>

#include <xuna/learning/PreferenceLearnerV2.h>

namespace xuna {

// Online, prequential metrics for a learner. Call score() before applying the
// corresponding feedback so the metric measures an out-of-sample prediction.
// All values are bounded aggregates; no samples or heap storage are retained.
struct LearningEvaluation {
  uint32_t samples = 0;
  uint32_t correct = 0;
  uint32_t approved = 0;
  uint64_t brierQ32 = 0;
  uint64_t baselineBrierQ32 = 0;

  uint32_t accuracyQ16() const {
    return samples == 0
               ? 0
               : static_cast<uint32_t>((uint64_t(correct) << 16) / samples);
  }

  // Positive gain means the learner's Brier score beats a 50/50 baseline.
  int64_t brierGainQ32() const {
    return static_cast<int64_t>(baselineBrierQ32) -
           static_cast<int64_t>(brierQ32);
  }
};

class PrequentialEvaluator {
 public:
  bool score(const PreferenceLearnerV2& learner,
             const LearningDecision& decision, bool approved) {
    if (decision.id == 0 || decision.context >= learner.config().contextCount ||
        decision.action >= learner.config().actionCount) {
      return false;
    }

    const uint32_t prediction =
        learner.posterior(decision.context, decision.action).meanQ16;
    const uint32_t outcome = approved ? 65535U : 0U;
    const int64_t difference = static_cast<int64_t>(prediction) - outcome;
    const int64_t baselineDifference =
        static_cast<int64_t>(32768U) - outcome;
    evaluation_.brierQ32 += static_cast<uint64_t>(difference * difference);
    evaluation_.baselineBrierQ32 +=
        static_cast<uint64_t>(baselineDifference * baselineDifference);
    if ((prediction >= 32768U) == approved) ++evaluation_.correct;
    if (approved) ++evaluation_.approved;
    if (evaluation_.samples != UINT32_MAX) ++evaluation_.samples;
    return true;
  }

  const LearningEvaluation& metrics() const { return evaluation_; }
  void reset() { evaluation_ = LearningEvaluation(); }

 private:
  LearningEvaluation evaluation_;
};

}  // namespace xuna
