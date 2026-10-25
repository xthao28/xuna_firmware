#pragma once

#include <cstdint>

#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/Affect.h>
#include <xuna/life/Drives.h>
#include <xuna/personality/EvidenceLedger.h>
#include <xuna/visual/VisualIntent.h>

namespace xuna {

// PE-4c (P-D4, plan §6): closed tags the backend derives from what the owner
// said in a chat turn; the app forwards them (BLE opcode 8, arg = wire value).
enum class ConversationTag : uint8_t {
  None = 0,
  Praise,
  Scold,
  Affection,
  Playful,
  CuriousQuestion,
  LongChat,
};

constexpr uint8_t kConversationTagCount = 7;

// Personality PE-2 (plan docs/roadmap/personality, 2026-10-24): the
// temperament genome, evidence ledger, drives and affect as one unit, fed
// with plain values so LifeCore can own it on the board. It watches what
// really happens (owner inputs, choices XUNA showed, pet / shoo). Expression
// reads it (Expression.h), and from PE-4a it leans XUNA's choices where
// learning has no answer (Leaning.h).
// PE-3: offsets, the evidence ledger, the journal and the counters survive a
// reboot through snapshot section 5 (PersonalityCodec). Drives and affect
// are transient, like needs: they restart at every boot.
//
// Evidence comes only from the owner or the world (research §8.2):
// - curiosity: a curious or exploratory choice the owner reacted to,
//   e = outcome − p̂ (prediction error, so a habit cannot inflate itself);
// - playfulness: the same for a happy choice;
// - sociability: the same for XUNA's greeting answer (a fixed ±1 made every
//   consistent owner max it out within ~3 months, PE-1d finding);
// - tempo: owner-session density (device observed): ≥ kLivelyInputs inputs
//   is lively (+1), ≤ kCalmInputs is calm (−1).
// - conversation tags (PE-4c, no confirmation, plan §6): praise, affection
//   and long chats → sociability +, scold → sociability −, playful →
//   playfulness +, curious questions → curiosity +; source
//   ConversationInferred (weight 0.1, ≤ device observed), at most
//   kConversationDailyCap tags a day, and the usual ≥ 4 sessions rule.
// Sensitivity, mood baseline/inertia, persistence, boredom and restfulness
// get no evidence (O1 delegated default: innate only).
// An owner session starts at an owner input after ≥ kSessionGapMs without
// one (no trusted time needed) and ends after that gap; consolidation runs
// at the end of each session.
class Personality {
 public:
  static constexpr uint32_t kSessionGapMs = 30UL * 60UL * 1000UL;  // as HabitPredictor
  static constexpr uint16_t kLivelyInputs = 8;   // [P]
  static constexpr uint16_t kCalmInputs = 2;     // [P]
  // Conversation tags felt per day of uptime (transient: a reboot restarts
  // the count, like drives and affect). [P]
  static constexpr uint8_t kConversationDailyCap = 8;
  static constexpr uint32_t kDayMs = 24UL * 60UL * 60UL * 1000UL;

  explicit Personality(uint32_t petSeed = 0, const LedgerConfig& ledger = LedgerConfig())
      : ledger_(ledger) {
    seed(petSeed);
  }

  // The pet's durable IdentityRecord seed. Before hatch (no identity) the
  // genome stays neutral (all 0). Offsets and evidence restart.
  void seed(uint32_t petSeed) {
    if (petSeed) Temperament::seed(genome_, petSeed);
    else genome_ = TemperamentState();
    ledger_.forget();
    session_ = evidence_ = shifts_ = decays_ = 0;
    refresh();
    ++revision_;
  }

  // Adopt a validated stored state (PersonalityCodec::load, boot only) on
  // top of the seeded baseline.
  void restore(const int16_t* offsets, uint32_t session, uint32_t evidence, uint32_t shifts,
               uint32_t decays, const TraitLedger* traits, const JournalEntry* journal,
               uint8_t head, uint16_t sequence) {
    for (uint8_t i = 0; i < kTraitCount; ++i) genome_.offset[i] = offsets[i];
    session_ = session;
    evidence_ = evidence;
    shifts_ = shifts;
    decays_ = decays;
    ledger_.restore(traits, journal, head, sequence);
    refresh();
    ++revision_;
  }

  void begin(uint32_t now) {
    drives_.reset(now);
    affect_.reset(now);
    open_ = false;
    last_ = Shown();
    talkStart_ = now;
    talkCount_ = 0;
  }

  // Any owner input (button, app command). Call before reaction(). True when
  // it opened a new owner session (the owner is back after kSessionGapMs).
  bool ownerInput(uint32_t now) {
    const bool opened = !open_;
    if (!open_) {
      open_ = true;
      ++session_;
      ++revision_;
      inputs_ = 0;
      affect_.ownerArrived(false);
    }
    lastOwner_ = now;
    if (inputs_ < 0xffff) ++inputs_;
    drives_.ownerContact();
    return opened;
  }

  // A Track A variant was shown: looking around satisfies explore. Bouncing
  // plays over a happy choice, which already satisfied play (acted).
  void expressed(VisualVariant variant) {
    if (variant == VisualVariant::LookAround) drives_.satisfyExplore();
  }

  // XUNA showed a learner decision: learner action index, LearnInteraction,
  // whether UCB explored, and the learner's p̂ for it (Q0.16).
  void shown(uint8_t action, uint8_t context, bool explore, uint16_t predictedQ16) {
    if (action >= kLearnActionCount) return;
    last_.valid = true;
    last_.action = action;
    last_.context = context;
    last_.explore = explore;
    last_.predictedQ16 = predictedQ16;
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

  // A conversation tag (PE-4c), after ownerInput(). XUNA feels it at once
  // and it is weak evidence for one trait. False when ignored: unknown tag,
  // no open session, or the daily cap is reached.
  bool conversation(ConversationTag tag, uint32_t now) {
    const uint8_t t = static_cast<uint8_t>(tag);
    if (t == 0 || t >= kConversationTagCount || !open_) return false;
    if (uint32_t(now - talkStart_) >= kDayMs) {
      talkStart_ = now;
      talkCount_ = 0;
    }
    if (talkCount_ >= kConversationDailyCap) return false;
    ++talkCount_;
    const int16_t half = kTraitOne / 2, quarter = kTraitOne / 4;
    switch (tag) {
      case ConversationTag::Praise:
        affect_.conversation(half, quarter);
        observe(TraitId::Sociability, kTraitOne, EvidenceSource::ConversationInferred,
                EvidenceReason::ConversationPraise);
        break;
      case ConversationTag::Scold:
        affect_.conversation(int16_t(-half), quarter);
        observe(TraitId::Sociability, int16_t(-kTraitOne), EvidenceSource::ConversationInferred,
                EvidenceReason::ConversationScold);
        break;
      case ConversationTag::Affection:
        affect_.conversation(half, 0);
        observe(TraitId::Sociability, kTraitOne, EvidenceSource::ConversationInferred,
                EvidenceReason::ConversationAffection);
        break;
      case ConversationTag::Playful:
        affect_.conversation(quarter, half);
        observe(TraitId::Playfulness, kTraitOne, EvidenceSource::ConversationInferred,
                EvidenceReason::ConversationPlay);
        break;
      case ConversationTag::CuriousQuestion:
        affect_.conversation(kTraitOne / 8, quarter);
        observe(TraitId::Curiosity, kTraitOne, EvidenceSource::ConversationInferred,
                EvidenceReason::ConversationQuestion);
        break;
      case ConversationTag::LongChat:
        affect_.conversation(quarter, 0);
        observe(TraitId::Sociability, kTraitOne, EvidenceSource::ConversationInferred,
                EvidenceReason::ConversationLong);
        break;
      case ConversationTag::None:
        break;
    }
    return true;
  }

  // XUNA showed `action` (whoever chose it): satisfies drives.
  void acted(ActionId action) { drives_.acted(action); }

  // Every step. `activity` and `energy` describe the time since the last call.
  void step(uint32_t now, Activity activity, uint16_t energy) {
    const bool resting = activity == Activity::Resting || activity == Activity::Sleeping;
    drives_.update(now, energy, resting);
    affect_.update(now, drives_.state().boredom);
    if (open_ && uint32_t(now - lastOwner_) >= kSessionGapMs) closeSession();
  }

  // ForgetAll: XUNA keeps who it was born as, forgets what it became.
  void forget() {
    Temperament::forget(genome_);
    ledger_.forget();
    refresh();
    ++revision_;
  }

  // Changes whenever something persisted changes (genome offsets, ledger,
  // journal, counters); LifeCore re-encodes section 5 only then.
  uint32_t revision() const { return revision_; }
  // Traits moved since the last call (TraitConsolidated, journal has them).
  uint8_t takeShifts() {
    const uint8_t n = newShifts_;
    newShifts_ = 0;
    return n;
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
    if (ledger_.observe(trait, value, source, reason, session_)) {
      ++evidence_;
      ++revision_;
    }
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
    if (r.shifted || r.decayed) ++revision_;
    if (r.shifted) refresh();
    newShifts_ = static_cast<uint8_t>(newShifts_ + r.shifted);
  }

  void refresh() {
    drives_.setRates(Drives::ratesFor(genome_));
    affect_.setParameters(Affect::parametersFor(genome_));
  }

  TemperamentState genome_;
  EvidenceLedger ledger_;
  Drives drives_;
  Affect affect_;
  Shown last_;
  bool open_ = false;
  uint32_t session_ = 0;
  uint32_t lastOwner_ = 0;
  uint16_t inputs_ = 0;
  uint32_t talkStart_ = 0;
  uint8_t talkCount_ = 0;
  uint32_t shifts_ = 0;
  uint32_t decays_ = 0;
  uint32_t evidence_ = 0;
  uint32_t revision_ = 0;
  uint8_t newShifts_ = 0;
};

}  // namespace xuna
