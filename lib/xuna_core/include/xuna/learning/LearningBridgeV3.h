#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/learning/PreferenceLearnerV3.h>
#include <xuna/life/LifeCommit.h>

namespace xuna {

// Shadow seam for learner v3 (plan §9.3/§9.4) next to the authoritative v1
// LifeEngine. v1 still chooses what XUNA shows; v3 observes the same
// decisions and explicit feedback so the two can be compared before any
// production switch. The bridge owns neither learner and changes no
// persistence bytes. Lab component since the S18 preparation (2026-10-15):
// LifeCore now uses ProductLearner (v3 decides, LifeSnapshot v2).
struct LearningBridgeV3Decision {
  uint32_t id = 0;
  uint32_t decidedAt = 0;
  uint8_t context = 0;
  uint8_t dayPart = 0;  // 0 = unknown until trusted time (S10)
  uint8_t baselineAction = 0;
  V3Decision candidate;
};

struct LearningBridgeV3Metrics {
  uint32_t decisions = 0;
  uint32_t agreements = 0;
  uint32_t candidateExploratory = 0;
  uint32_t feedbackAccepted = 0;
  uint32_t feedbackRejected = 0;
  uint32_t commitFailures = 0;
};

class LearningBridgeV3 {
 public:
  LearningBridgeV3(LifeEngine& baseline, PreferenceLearnerV3& candidate,
                   IRandom& random)
      : baseline_(baseline), candidate_(candidate), random_(random) {}

  LearningBridgeV3Decision choose(uint8_t context, uint8_t dayPart,
                                  uint32_t id, uint32_t now) {
    LearningBridgeV3Decision result;
    result.id = id;
    result.decidedAt = now;
    result.context = context;
    result.dayPart = dayPart;
    result.baselineAction = baseline_.choose(context);
    result.candidate = candidate_.choose(context, dayPart, id, 0xFF, random_);
    if (result.candidate.id == 0) return result;

    ++metrics_.decisions;
    if (result.candidate.explore) ++metrics_.candidateExploratory;
    if (result.baselineAction == result.candidate.action)
      ++metrics_.agreements;
    return result;
  }

  // Explicit feedback describes the action XUNA showed (v1's choice). v3 is
  // credited on that same shown action, which is what the owner rated.
  //
  // Plan §9.5 order: validate everything first, stage v1 with the feedback
  // applied, commit the staged state through `commit` (if given), and only
  // then apply to v1 and v3 in RAM. A failed commit changes nothing and
  // returns CommitFailed so the caller can report not_ready and allow a
  // retry. Without a committer the change is applied directly (lab use).
  enum class Result : uint8_t { Applied = 0, Rejected, CommitFailed };

  Result feedback(const LearningBridgeV3Decision& decision, bool approved,
                  uint8_t source, uint32_t now, ILifeCommit* commit) {
    (void)now;
    V3Decision shown = decision.candidate;
    shown.action = decision.baselineAction;
    if (source < 1 || source > 2 || decision.candidate.id == 0 ||
        decision.candidate.id != decision.id || decision.context > 1 ||
        decision.baselineAction > 1 || !candidate_.accepts(shown)) {
      ++metrics_.feedbackRejected;
      return Result::Rejected;
    }
    shown.predictedQ16 =
        candidate_.view(decision.context, decision.dayPart, shown.action).pQ16;

    LifeEngine staged = baseline_;
    staged.feedback(decision.context, decision.baselineAction, approved,
                    source);
    if (commit != 0 && !commit->commit(staged)) {
      ++metrics_.commitFailures;
      return Result::CommitFailed;
    }
    baseline_ = staged;
    candidate_.feedback(shown, approved);  // accepted above
    ++metrics_.feedbackAccepted;
    return Result::Applied;
  }

  bool feedback(const LearningBridgeV3Decision& decision, bool approved,
                uint8_t source, uint32_t now) {
    return feedback(decision, approved, source, now, 0) == Result::Applied;
  }

  const LearningBridgeV3Metrics& metrics() const { return metrics_; }
  void resetMetrics() { metrics_ = LearningBridgeV3Metrics(); }

 private:
  LifeEngine& baseline_;
  PreferenceLearnerV3& candidate_;
  IRandom& random_;
  LearningBridgeV3Metrics metrics_;
};

}  // namespace xuna
