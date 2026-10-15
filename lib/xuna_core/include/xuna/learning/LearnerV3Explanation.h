#pragma once

#include <cstdint>

#include <xuna/learning/PreferenceLearnerV3.h>

namespace xuna {

// Plan §9.6 explanation for learner v3, built only from evidence fields.
// The phone turns it into text such as "XUNA chose wave because you liked it
// 12 of 15 times in the evening"; the device never generates prose.
enum class V3Reason : uint8_t {
  Invalid = 0,
  HighestEstimate,  // shown action was the greedy choice
  Exploration,      // shown action was chosen to learn more
  NoEvidence,       // no rated or stated evidence yet in this context
  Told,             // no ratings yet; the owner's statements decided
};

struct V3ExplanationCell {
  uint8_t action = 0;
  uint16_t pQ16 = 0;
  uint32_t nEffQ16 = 0;
  uint32_t positiveQ16 = 0;
  uint32_t negativeQ16 = 0;
  uint16_t rated = 0;
  bool fine = false;  // evidence from the day-part cell, not the coarse one
  // Part of positive/negative the owner stated (plan §9.6 statedMass), so
  // "bạn đã nói" can be shown apart from "bạn đã thích".
  uint32_t statedPositiveQ16 = 0;
  uint32_t statedNegativeQ16 = 0;
};

struct V3Explanation {
  bool valid = false;
  uint8_t interaction = 0;
  uint8_t dayPart = 0;
  uint8_t actionCount = 0;
  uint8_t shownAction = 0;
  uint8_t greedyAction = 0;
  uint16_t marginQ16 = 0;  // greedy estimate minus the best alternative
  uint32_t policyVersion = 0;
  V3Reason reason = V3Reason::Invalid;
  V3ExplanationCell cells[LearnerV3Config::kMaxActions];
};

class LearnerV3Explainer {
 public:
  static V3Explanation explain(const PreferenceLearnerV3& learner,
                               const V3Decision& decision) {
    V3Explanation e;
    const LearnerV3Config& c = learner.config();
    if (decision.id == 0 || decision.interaction >= c.interactions ||
        decision.dayPart >= c.dayParts || decision.action >= c.actions)
      return e;

    e.valid = true;
    e.interaction = decision.interaction;
    e.dayPart = decision.dayPart;
    e.actionCount = c.actions;
    e.shownAction = decision.action;
    e.policyVersion = decision.policyVersion;
    e.greedyAction = learner.greedy(decision.interaction, decision.dayPart);

    uint32_t totalRated = 0;
    uint32_t totalStated = 0;
    uint16_t best = 0, second = 0;
    for (uint8_t a = 0; a < c.actions; ++a) {
      const V3CellView view =
          learner.view(decision.interaction, decision.dayPart, a);
      V3ExplanationCell& cell = e.cells[a];
      cell.action = a;
      cell.pQ16 = view.pQ16;
      cell.nEffQ16 = view.nEffQ16;
      cell.positiveQ16 = view.positiveQ16;
      cell.negativeQ16 = view.negativeQ16;
      cell.rated = view.rated;
      cell.fine = view.fine;
      cell.statedPositiveQ16 = view.statedPositiveQ16;
      cell.statedNegativeQ16 = view.statedNegativeQ16;
      totalRated += view.rated;
      totalStated += view.statedPositiveQ16 + view.statedNegativeQ16;
      if (a == e.greedyAction) {
        best = view.pQ16;
      } else if (view.pQ16 > second) {
        second = view.pQ16;
      }
    }
    e.marginQ16 = best > second ? static_cast<uint16_t>(best - second) : 0;
    if (totalRated == 0) {
      e.reason = totalStated == 0 ? V3Reason::NoEvidence : V3Reason::Told;
    } else if (decision.explore) {
      e.reason = V3Reason::Exploration;
    } else {
      e.reason = V3Reason::HighestEstimate;
    }
    return e;
  }
};

}  // namespace xuna
