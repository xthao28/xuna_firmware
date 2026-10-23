#pragma once

#include <cstdint>

#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/Affect.h>
#include <xuna/life/Drives.h>
#include <xuna/life/LifeCore.h>
#include <xuna/personality/EvidenceLedger.h>

namespace xuna {

// Personality PE-1d: the temperament genome, evidence ledger, drives and
// affect running next to BoardLab in shadow mode. They watch what really
// happens (owner inputs, choices XUNA showed, pet / shoo) and never change
// XUNA's behaviour. Host / Life Lab only.
//
// Evidence comes only from the owner or the world (research §8.2):
// - curiosity: a curious or exploratory choice the owner reacted to,
//   e = outcome − p̂ (prediction error, so a habit cannot inflate itself);
// - playfulness: the same for a happy choice;
// - sociability: the same for XUNA's greeting answer (a fixed ±1 made every
//   consistent owner max it out within ~3 months, PE-1d finding);
// - tempo: owner-session density (device observed): ≥ kLivelyInputs inputs
//   is lively (+1), ≤ kCalmInputs is calm (−1).
// Sensitivity, mood baseline/inertia, persistence, boredom and restfulness
// get no evidence (O1 delegated default: innate only).
// An owner session starts at an owner input after ≥ kSessionGapMs without
// one (no trusted time needed) and ends after that gap; consolidation runs
// at the end of each session.
struct ShadowConfig {
  LedgerConfig ledger;
  uint16_t driveWeightQ16 = 16384;  // a full drive is worth +0.25 approval [P]
};

class PersonalityShadow {
 public:
  static constexpr uint32_t kSessionGapMs = 30UL * 60UL * 1000UL;  // as HabitPredictor
  static constexpr uint16_t kLivelyInputs = 8;   // [P]
  static constexpr uint16_t kCalmInputs = 2;     // [P]

  explicit PersonalityShadow(uint32_t petSeed, const ShadowConfig& config = ShadowConfig())
      : config_(config), ledger_(config.ledger) {
    Temperament::seed(genome_, petSeed);
    refresh();
  }

  void begin(uint32_t now) {
    drives_.reset(now);
    affect_.reset(now);
    open_ = false;
  }

  // Any owner input (button, app command). Call before reaction().
  void ownerInput(uint32_t now) {
    if (!open_) {
      open_ = true;
      ++session_;
      inputs_ = 0;
      affect_.ownerArrived(false);
    }
    lastOwner_ = now;
    if (inputs_ < 0xffff) ++inputs_;
    drives_.ownerContact();
  }

  // XUNA showed a learner decision (LifeCoreStepOutput::decisionShown).
  void shown(const LifeCoreStepOutput& out, const PreferenceLearnerV3& learner) {
    uint8_t action = 0;
    if (!learnIndexFor(static_cast<ActionId>(out.selectedAction), action)) return;
    last_.valid = true;
    last_.action = action;
    last_.context = out.decisionContext;
    last_.explore = out.selectedExplore;
    last_.predictedQ16 = learner.view(out.decisionContext, out.decisionDayPart, action).pQ16;
  }

  // The owner's pet / shoo that the device applied to the shown decision.
  void reaction(bool pet) {
    if (!last_.valid || !open_) return;
    last_.valid = false;
    affect_.feedback(pet, last_.predictedQ16);
    const int32_t error = (pet ? kTraitOne : 0) - (last_.predictedQ16 >> 2);
    const int16_t e = static_cast<int16_t>(error);
    if (last_.context == static_cast<uint8_t>(LearnInteraction::ButtonGreeting)) {
      observe(TraitId::Sociability, e, EvidenceSource::OwnerReaction,
              e >= 0 ? EvidenceReason::GreetingPetted : EvidenceReason::GreetingShooed);
    } else if (last_.action == 2 || last_.explore) {  // curious, or trying something
      observe(TraitId::Curiosity, e, EvidenceSource::OwnerReaction,
              e >= 0 ? EvidenceReason::ExploreRewarded : EvidenceReason::ExploreShooed);
    } else if (last_.action == 1) {  // happy
      observe(TraitId::Playfulness, e, EvidenceSource::OwnerReaction,
              e >= 0 ? EvidenceReason::PlayRewarded : EvidenceReason::PlayShooed);
    }
  }

  // Every loop, after the BoardLab step.
  void step(uint32_t now, const LifeCoreStepOutput& out) {
    const bool resting = out.activity == Activity::Resting || out.activity == Activity::Sleeping;
    drives_.update(now, out.needs.energy, resting);
    affect_.update(now, drives_.state().boredom);
    if (open_ && uint32_t(now - lastOwner_) >= kSessionGapMs) closeSession();
  }

  // What XUNA would show if drives counted: the learner's estimate plus a
  // drive bonus (explore → curious, play → happy) minus boredom for repeating
  // the last action. Ties go to the lower index. Shadow only.
  uint8_t choose(uint8_t interaction, uint8_t dayPart, const PreferenceLearnerV3& learner) const {
    const DriveState& d = drives_.state();
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
    drives_.acted(id);
  }

  const TemperamentState& genome() const { return genome_; }
  const EvidenceLedger& ledger() const { return ledger_; }
  const Drives& drives() const { return drives_; }
  const Affect& affect() const { return affect_; }
  uint32_t sessions() const { return session_; }
  uint32_t shifts() const { return shifts_; }
  uint32_t decays() const { return decays_; }
  uint32_t evidence() const { return evidence_; }

 private:
  struct Shown {
    bool valid = false;
    uint8_t action = 0;
    uint8_t context = 0;
    bool explore = false;
    uint16_t predictedQ16 = 0;
  };

  void observe(TraitId trait, int16_t value, EvidenceSource source, EvidenceReason reason) {
    if (ledger_.observe(trait, value, source, reason, session_)) ++evidence_;
  }

  void closeSession() {
    open_ = false;
    if (inputs_ >= kLivelyInputs)
      observe(TraitId::Tempo, kTraitOne, EvidenceSource::DeviceObserved, EvidenceReason::LivelySession);
    else if (inputs_ <= kCalmInputs)
      observe(TraitId::Tempo, int16_t(-kTraitOne), EvidenceSource::DeviceObserved,
              EvidenceReason::CalmSession);
    const ConsolidationReport r = ledger_.consolidate(genome_);
    shifts_ += r.shifted;
    decays_ += r.decayed;
    if (r.shifted) refresh();
  }

  void refresh() {
    drives_.setRates(Drives::ratesFor(genome_));
    affect_.setParameters(Affect::parametersFor(genome_));
  }

  int32_t bonus(uint16_t drive) const {
    return int32_t((uint32_t(drive) * config_.driveWeightQ16) >> 16);
  }

  ShadowConfig config_;
  TemperamentState genome_;
  EvidenceLedger ledger_;
  Drives drives_;
  Affect affect_;
  Shown last_;
  bool open_ = false;
  uint32_t session_ = 0;
  uint32_t lastOwner_ = 0;
  uint16_t inputs_ = 0;
  uint8_t lastAction_ = 0xff;
  uint32_t shifts_ = 0;
  uint32_t decays_ = 0;
  uint32_t evidence_ = 0;
};

}  // namespace xuna
