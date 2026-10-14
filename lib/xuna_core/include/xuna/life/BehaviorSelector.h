#pragma once

#include <cstddef>
#include <cstdint>
#include <xuna/base/Random.h>
#include <xuna/life/ActionRegistry.h>
#include <xuna/life/Needs.h>
#include <climits>
#include <xuna/visual/VisualIntent.h>

namespace xuna {

struct SelectorWeights {
  int16_t novelty = 2;
  int16_t energyNeed = 1;
  int16_t arousal = 1;
  int16_t valence = 1;
};

struct BehaviorCandidate {
  ActionId action = ActionId::Idle;
  uint16_t novelty = 0;
  int16_t bias = 0;
  uint16_t energyCost = 0;
  bool allowed = true;
  bool cooldownReady = true;

  // C++11 member initializers make this a non-aggregate; keep brace tables.
  BehaviorCandidate(ActionId actionValue = ActionId::Idle,
                    uint16_t noveltyValue = 0, int16_t biasValue = 0,
                    uint16_t energyCostValue = 0, bool allowedValue = true,
                    bool cooldownReadyValue = true)
      : action(actionValue),
        novelty(noveltyValue),
        bias(biasValue),
        energyCost(energyCostValue),
        allowed(allowedValue),
        cooldownReady(cooldownReadyValue) {}
};

struct SelectionResult {
  bool found = false;
  ActionId action = ActionId::Idle;
  int32_t score = 0;
};

class BehaviorSelector {
 public:
  static SelectionResult choose(const BehaviorCandidate* candidates,
                                size_t count, const NeedsState& needs,
                                const MoodState& mood, IRandom& random,
                                const SelectorWeights& weights = SelectorWeights()) {
    SelectionResult result;
    int32_t best = INT32_MIN;
    size_t bestCount = 0;
    for (size_t i = 0; i < count; ++i) {
      const BehaviorCandidate& candidate = candidates[i];
      if (!candidate.allowed || !candidate.cooldownReady ||
          candidate.energyCost > needs.energy)
        continue;
      const int32_t score = scoreFor(candidate, needs, mood, weights);
      if (!result.found || score > best) {
        best = score;
        bestCount = 0;
        result.found = true;
      }
      if (result.found && score == best) ++bestCount;
    }
    if (!result.found) return result;
    const uint32_t selected = random.below(static_cast<uint32_t>(bestCount));
    size_t seen = 0;
    for (size_t i = 0; i < count; ++i) {
      const BehaviorCandidate& candidate = candidates[i];
      if (!candidate.allowed || !candidate.cooldownReady ||
          candidate.energyCost > needs.energy ||
          scoreFor(candidate, needs, mood, weights) != best)
        continue;
      if (seen++ == selected) {
        result.action = candidate.action;
        result.score = best;
        return result;
      }
    }
    result.found = false;
    return result;
  }

 private:
  static int32_t scoreFor(const BehaviorCandidate& candidate,
                          const NeedsState& needs, const MoodState& mood,
                          const SelectorWeights& weights) {
    const int32_t energyNeed = 65535 - needs.energy;
    const int32_t valence = static_cast<int32_t>(mood.valence) + 32768;
    const int64_t weighted =
        int64_t(candidate.novelty) * weights.novelty +
        int64_t(energyNeed) * weights.energyNeed +
        int64_t(mood.arousal) * weights.arousal +
        int64_t(valence) * weights.valence;
    return candidate.bias + static_cast<int32_t>(weighted / 65535);
  }
};

}  // namespace xuna