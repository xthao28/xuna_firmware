#pragma once

#include <cstdint>

#include <xuna/learning/PreferenceLearnerV3.h>
#include <xuna/life/ActionRegistry.h>

namespace xuna {

// Approved learning vocabulary (D5, owner 2026-10-15; plan §9.2). The learner
// works with dense indices; this table maps them to stable protocol ActionIds.
enum class LearnInteraction : uint8_t {
  QuietRequest = 0,    // app quiet context (A5)
  PlayRequest = 1,     // app play context (A5)
  ButtonGreeting = 2,  // GPIO0 press: XUNA chooses how to answer
};

constexpr uint8_t kLearnInteractionCount = 3;
constexpr uint8_t kLearnActionCount = 3;

// Learner index 0/1/2 → idle/happy/curious (existing art). Index order keeps
// v1's actions 0/1 at the same positions so v1 counts migrate unchanged.
static const ActionId kLearnActions[kLearnActionCount] = {
    ActionId::Idle, ActionId::Happy, ActionId::Curious};

inline bool learnActionId(uint8_t index, ActionId& action) {
  if (index >= kLearnActionCount) return false;
  action = kLearnActions[index];
  return true;
}

inline bool learnIndexFor(ActionId action, uint8_t& index) {
  for (uint8_t i = 0; i < kLearnActionCount; ++i) {
    if (kLearnActions[i] == action) {
      index = i;
      return true;
    }
  }
  return false;
}

// Product learner shape and the Life Lab sweep winner (values Proposed).
// 3 interactions × 5 day parts × 3 actions → 458-byte learner payload.
inline LearnerV3Config productLearnerConfig() {
  LearnerV3Config config;
  config.interactions = kLearnInteractionCount;
  config.dayParts = LearnerV3Config::kMaxDayParts;
  config.actions = kLearnActionCount;
  config.policy = ExplorePolicy::Ucb;
  config.gammaQ16 = 58982;          // 0.90
  config.priorStrengthQ16 = 65536;  // k = 1
  config.fineMinQ16 = 65536;        // m = 1
  config.ucbBonusQ16 = 9830;        // 0.15
  return config;
}

}  // namespace xuna
