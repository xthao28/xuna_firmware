#pragma once

#include <cstdint>

#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/LifeCore.h>
#include <xuna/personality/Personality.h>

namespace xuna {

// Personality PE-1d: Personality (genome, evidence ledger, drives, affect)
// running next to BoardLab in shadow mode, plus the lab-only "shadow world"
// choice that lets drives compete with the learner. It watches what really
// happens and never changes XUNA's behaviour. Host / Life Lab only; the
// evidence rules live in personality/Personality.h (shared with the board).
struct ShadowConfig {
  LedgerConfig ledger;
  uint16_t driveWeightQ16 = 16384;  // a full drive is worth +0.25 approval [P]
};

class PersonalityShadow {
 public:
  static constexpr uint32_t kSessionGapMs = Personality::kSessionGapMs;
  static constexpr uint16_t kLivelyInputs = Personality::kLivelyInputs;
  static constexpr uint16_t kCalmInputs = Personality::kCalmInputs;

  explicit PersonalityShadow(uint32_t petSeed, const ShadowConfig& config = ShadowConfig())
      : config_(config), p_(petSeed, config.ledger) {}

  void begin(uint32_t now) { p_.begin(now); }

  // Any owner input (button, app command). Call before reaction().
  void ownerInput(uint32_t now) { p_.ownerInput(now); }

  // XUNA showed a learner decision (LifeCoreStepOutput::decisionShown).
  void shown(const LifeCoreStepOutput& out, const PreferenceLearnerV3& learner) {
    uint8_t action = 0;
    if (!learnIndexFor(static_cast<ActionId>(out.selectedAction), action)) return;
    p_.shown(action, out.decisionContext, out.selectedExplore,
             learner.view(out.decisionContext, out.decisionDayPart, action).pQ16);
  }

  // The owner's pet / shoo that the device applied to the shown decision.
  void reaction(bool pet) { p_.reaction(pet); }

  // Every loop, after the BoardLab step.
  void step(uint32_t now, const LifeCoreStepOutput& out) {
    p_.step(now, out.activity, out.needs.energy);
  }

  // What XUNA would show if drives counted: the learner's estimate plus a
  // drive bonus (explore → curious, play → happy) minus boredom for repeating
  // the last action. Ties go to the lower index. Shadow only.
  uint8_t choose(uint8_t interaction, uint8_t dayPart, const PreferenceLearnerV3& learner) const {
    const DriveState& d = p_.drives().state();
    uint8_t best = 0;
    int32_t bestScore = 0;
    for (uint8_t a = 0; a < kLearnActionCount; ++a) {
      int32_t score = learner.view(interaction, dayPart, a).pQ16;
      if (a == 2) score += bonus(d.explore);
      if (a == 1) score += bonus(d.play);
      if (a == lastAction_) score -= bonus(d.boredom);
      if (a == 0 || score > bestScore) {
        best = a;
        bestScore = score;
      }
    }
    return best;
  }
  // The shadow world's action: drives are satisfied by what XUNA would have
  // shown; affect and evidence still come from reactions to what it showed.
  void noteShadowAction(uint8_t action) {
    ActionId id = ActionId::Idle;
    if (!learnActionId(action, id)) return;
    lastAction_ = action;
    p_.acted(id);
  }

  const TemperamentState& genome() const { return p_.genome(); }
  const EvidenceLedger& ledger() const { return p_.ledger(); }
  const Drives& drives() const { return p_.drives(); }
  const Affect& affect() const { return p_.affect(); }
  uint32_t sessions() const { return p_.sessions(); }
  uint32_t shifts() const { return p_.shifts(); }
  uint32_t decays() const { return p_.decays(); }
  uint32_t evidence() const { return p_.evidence(); }

 private:
  int32_t bonus(uint16_t drive) const {
    return int32_t((uint32_t(drive) * config_.driveWeightQ16) >> 16);
  }

  ShadowConfig config_;
  Personality p_;
  uint8_t lastAction_ = 0xff;
};

}  // namespace xuna
