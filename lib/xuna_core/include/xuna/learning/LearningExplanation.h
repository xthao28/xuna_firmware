#pragma once

#include <cstdint>

#include <xuna/learning/PreferenceLearnerV2.h>

namespace xuna {

enum class LearningReason : uint8_t {
  Invalid = 0,
  Exploration = 1,
  HighestPosterior = 2,
};

struct LearningExplanationCell {
  uint8_t action = 0;
  uint16_t positive = 0;
  uint16_t negative = 0;
  uint16_t meanQ16 = 0;
  uint16_t count = 0;
  bool selected = false;
};

struct LearningExplanation {
  bool valid = false;
  uint8_t context = 0;
  uint8_t actionCount = 0;
  uint8_t selectedAction = 0;
  uint16_t selectedMeanQ16 = 0;
  uint16_t alternativeMeanQ16 = 0;
  uint16_t marginQ16 = 0;
  uint32_t policyVersion = 0;
  LearningReason reason = LearningReason::Invalid;
  LearningExplanationCell cells[PreferenceLearnerV2::kMaxActions] = {};
};

class LearningExplainer {
 public:
  static LearningExplanation explain(const PreferenceLearnerV2& learner,
                                     const LearningDecision& decision) {
    LearningExplanation explanation;
    if (decision.id == 0 || decision.context >= learner.config().contextCount ||
        decision.action >= learner.config().actionCount) {
      return explanation;
    }

    explanation.valid = true;
    explanation.context = decision.context;
    explanation.actionCount = learner.config().actionCount;
    explanation.selectedAction = decision.action;
    explanation.policyVersion = decision.policyVersion;
    explanation.reason = decision.exploratory
                             ? LearningReason::Exploration
                             : LearningReason::HighestPosterior;

    uint16_t alternative = 0;
    for (uint8_t action = 0; action < learner.config().actionCount; ++action) {
      const LearningPosterior posterior = learner.posterior(decision.context,
                                                             action);
      LearningExplanationCell& cell = explanation.cells[action];
      cell.action = action;
      cell.positive = posterior.positive;
      cell.negative = posterior.negative;
      cell.meanQ16 = posterior.meanQ16;
      cell.count = posterior.count;
      cell.selected = action == decision.action;
      if (action == decision.action) {
        explanation.selectedMeanQ16 = posterior.meanQ16;
      } else if (posterior.meanQ16 > alternative) {
        alternative = posterior.meanQ16;
      }
    }
    explanation.alternativeMeanQ16 = alternative;
    explanation.marginQ16 = explanation.selectedMeanQ16 >= alternative
                                ? static_cast<uint16_t>(
                                      explanation.selectedMeanQ16 - alternative)
                                : 0;
    return explanation;
  }
};

}  // namespace xuna
