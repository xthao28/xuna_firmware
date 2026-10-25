#pragma once

#include <cstdint>

#include <xuna/personality/Temperament.h>

namespace xuna {

// Personality PE-1a: evidence ledger and consolidation (research §8.2, §9.2;
// plan P-D4 §6). Lab only until gate G1. All weights and thresholds are [P].
//
// Evidence never touches the genome directly. It accumulates per trait as
// `pending` (Σ w·e) and `importance` (Σ |w·e|), grouped by owner session;
// one session adds at most ±kMaxSessionShare to each, so a single session of
// button spam weighs no more than an ordinary strong session.
// consolidate() moves a trait only when
//   importance ≥ kMinImportance and the evidence spans ≥ kMinSessions
//   sessions, ≥ 2/3 of those sessions agree with the sign of pending, and
//   the evidence is coherent: |pending| ≥ importance / 2.
// Then offset += kMaxStep · pending / importance (≤ 0.03 per step) and the
// trait's ledger restarts. If the sessions disagree, pending and importance
// shrink to a fifth and the session count restarts (JPAF's "no" branch,
// deterministic). Self-produced evidence (XUNA's own actions) has weight 0.

// Who says so. Weights follow the learner (rating 1, stated 0.5, reaction 0.3).
enum class EvidenceSource : uint8_t {
  Self = 0,              // XUNA's own behaviour: never evidence (anti self-loop)
  OwnerRating,           // app rating (legacy opcode 3)
  OwnerStated,           // confirmed chat statement (D6)
  OwnerReaction,         // pet / shoo on the device (D6 revised)
  DeviceObserved,        // sessions, drive reduction (L-D4)
  ConversationInferred,  // closed LLM tag, no confirmation (P-D4)
};

constexpr uint8_t kEvidenceSourceCount = 6;

// Why, for the Journal ("vì bạn hay khen khi nó khám phá"). Producers are
// wired in PE-1d; the list is Proposed and may grow (append only).
enum class EvidenceReason : uint8_t {
  None = 0,
  ExploreRewarded,     // owner petted an exploratory choice
  ExploreShooed,       // owner shooed an exploratory choice
  GreetingPetted,      // owner petted XUNA's greeting answer
  GreetingShooed,      // owner shooed XUNA's greeting answer
  RegularSessions,     // owner comes back regularly
  LivelySession,       // dense interaction in a session
  CalmSession,         // quiet session, few presses
  FeedbackSurprise,    // feedback far from what XUNA expected
  ConversationPraise,  // P-D4 tag
  ConversationScold,   // P-D4 tag
  PlayRewarded,        // owner petted a playful (happy) choice
  PlayShooed,          // owner shooed a playful (happy) choice
  // PE-4c (2026-10-25): the rest of the closed conversation tags.
  ConversationAffection,
  ConversationPlay,
  ConversationQuestion,
  ConversationLong,
};

constexpr uint8_t kEvidenceReasonCount = 17;

struct JournalEntry {
  uint16_t sequence = 0;  // 0 = empty slot
  uint8_t trait = 0;
  uint8_t reason = 0;     // dominant EvidenceReason
  uint8_t source = 0;     // dominant EvidenceSource
  uint8_t sessions = 0;   // sessions that backed the change (saturating)
  int16_t delta = 0;      // applied offset change, Q1.14
};

struct TraitLedger {
  int32_t pending = 0;      // Σ w·e, Q1.14, clamped to ±kMaxPending
  int32_t importance = 0;   // Σ |w·e|, Q1.14, ≤ kMaxSessionShare per session
  uint32_t lastSession = 0; // session of the latest evidence (0 = none)
  int32_t sessionNet = 0;   // Σ w·e within lastSession (uncapped)
  int32_t sessionMagnitude = 0;  // Σ |w·e| within lastSession (uncapped)
  uint8_t sessions = 0;     // closed sessions with evidence (saturating)
  uint8_t positive = 0;     // closed sessions with net > 0
  uint8_t negative = 0;     // closed sessions with net < 0
  uint16_t reasonWeight[kEvidenceReasonCount] = {};  // Σ |w·e| >> 6
  uint16_t sourceWeight[kEvidenceSourceCount] = {};  // Σ |w·e| >> 6
};

// Consolidation thresholds. Life Lab sweeps them. Defaults: G1-a (owner
// 2026-10-24) raised minSessions 3 → 4 (random-owner drift 0.008 → 0.003,
// alternating swing 0.009, signal ≈ +0.12 per month); the rest are PE-1a.
struct LedgerConfig {
  int32_t minImportance = 24576;    // 1.5 [P]
  uint8_t minSessions = 4;          // [P] G1-a
  int32_t coherenceQ14 = 8192;      // |pending| ≥ 0.5 · importance [P]
  int32_t maxStep = 492;            // 0.03 [P]
  int32_t maxSessionShare = 8192;   // 0.5 per session [P]
};

struct ConsolidationReport {
  uint8_t shifted = 0;   // traits moved
  uint8_t decayed = 0;   // traits whose evidence disagreed
};

class EvidenceLedger {
 public:
  static constexpr uint8_t kJournalSize = 4;
  static constexpr int32_t kMinImportance = 24576;  // LedgerConfig defaults
  static constexpr uint8_t kMinSessions = 4;        // [P] G1-a
  static constexpr int32_t kMaxStep = 492;          // 0.03 [P]
  static constexpr int32_t kMaxPending = 65536;     // ±4.0 [P]
  static constexpr int32_t kMaxSessionShare = 8192; // 0.5 per session [P]

  explicit EvidenceLedger(const LedgerConfig& config = LedgerConfig()) : config_(config) {}
  const LedgerConfig& config() const { return config_; }

  static int16_t weight(EvidenceSource source) {
    switch (source) {
      case EvidenceSource::Self: return 0;
      case EvidenceSource::OwnerRating: return 16384;           // 1.0
      case EvidenceSource::OwnerStated: return 8192;            // 0.5
      case EvidenceSource::OwnerReaction: return 4915;          // 0.3
      case EvidenceSource::DeviceObserved: return 1638;         // 0.1
      case EvidenceSource::ConversationInferred: return 1638;   // 0.1, ≤ device
    }
    return 0;
  }

  // One piece of evidence: `value` ∈ [−1, 1] in Q1.14 says which way the
  // trait should go; `session` is the caller's owner-session counter (any
  // non-zero id; a new id opens a new session). Returns false when ignored.
  bool observe(TraitId trait, int16_t value, EvidenceSource source,
               EvidenceReason reason, uint32_t session) {
    const uint8_t t = static_cast<uint8_t>(trait);
    const uint8_t s = static_cast<uint8_t>(source);
    const uint8_t r = static_cast<uint8_t>(reason);
    if (t >= kTraitCount || s >= kEvidenceSourceCount ||
        r >= kEvidenceReasonCount || session == 0)
      return false;
    if (value > kTraitOne) value = kTraitOne;
    if (value < -kTraitOne) value = -kTraitOne;
    const int32_t contribution = (int32_t(weight(source)) * value) / kTraitOne;
    if (contribution == 0) return false;
    TraitLedger& l = traits_[t];
    if (session != l.lastSession) {
      closeSession(l);
      l.lastSession = session;
    }
    const int32_t magnitude = contribution < 0 ? -contribution : contribution;
    // Only the part of this session still under its share reaches the ledger.
    const int32_t share = config_.maxSessionShare;
    const int32_t net = clamp(l.sessionNet, share);
    const int32_t mag = l.sessionMagnitude < share ? l.sessionMagnitude : share;
    l.sessionNet = clamp(l.sessionNet + contribution, kMaxPending);
    l.sessionMagnitude = l.sessionMagnitude > kMaxPending - magnitude
                             ? kMaxPending : l.sessionMagnitude + magnitude;
    const int32_t newMag = l.sessionMagnitude < share ? l.sessionMagnitude : share;
    l.pending = clamp(l.pending + clamp(l.sessionNet, share) - net, kMaxPending);
    l.importance += newMag - mag;
    addWeight(l.reasonWeight[r], magnitude);
    addWeight(l.sourceWeight[s], magnitude);
    return true;
  }

  // Importance-triggered consolidation; call at the end of an owner session
  // (or any time: it does nothing until a trait is ready). Deterministic.
  ConsolidationReport consolidate(TemperamentState& temperament) {
    ConsolidationReport report;
    for (uint8_t t = 0; t < kTraitCount; ++t) {
      TraitLedger& l = traits_[t];
      uint32_t sessions = l.sessions, positive = l.positive, negative = l.negative;
      if (l.lastSession != 0) {  // the open session counts, without closing it
        ++sessions;
        if (l.sessionNet > 0) ++positive;
        if (l.sessionNet < 0) ++negative;
      }
      if (l.importance < config_.minImportance || sessions < config_.minSessions) continue;
      const uint32_t agreeing = l.pending > 0 ? positive : l.pending < 0 ? negative : 0;
      const int32_t strength = l.pending < 0 ? -l.pending : l.pending;
      if (agreeing * 3 >= sessions * 2 &&
          int64_t(strength) * kTraitOne >= int64_t(config_.coherenceQ14) * l.importance) {
        const int32_t step = int32_t((int64_t(config_.maxStep) * l.pending) / l.importance);
        const int16_t applied =
            Temperament::shift(temperament, static_cast<TraitId>(t), static_cast<int16_t>(step));
        record(t, l, applied, sessions);
        l = TraitLedger();
        ++report.shifted;
      } else {
        l.pending /= 5;
        l.importance /= 5;
        l.sessions = 0;
        l.positive = 0;
        l.negative = 0;
        l.lastSession = 0;
        l.sessionNet = 0;
        l.sessionMagnitude = 0;
        ++report.decayed;
      }
    }
    return report;
  }

  // ForgetAll: drop evidence and the journal (the genome's offset is reset by
  // Temperament::forget; the baseline stays).
  void forget() {
    for (uint8_t t = 0; t < kTraitCount; ++t) traits_[t] = TraitLedger();
    for (uint8_t i = 0; i < kJournalSize; ++i) journal_[i] = JournalEntry();
    head_ = 0;
    sequence_ = 0;
  }

  const TraitLedger& trait(TraitId trait) const {
    return traits_[static_cast<uint8_t>(trait) % kTraitCount];
  }

  // Newest first; `age` 0 = latest. Empty slots have sequence 0.
  const JournalEntry& journal(uint8_t age) const {
    return journal_[(head_ + kJournalSize - 1 - age % kJournalSize) % kJournalSize];
  }

  // Raw state for persistence (PersonalityCodec, PE-3): ring slots in
  // storage order, the next slot to write and the last sequence used.
  const JournalEntry& journalSlot(uint8_t slot) const { return journal_[slot % kJournalSize]; }
  uint8_t journalHead() const { return head_; }
  uint16_t journalSequence() const { return sequence_; }
  // Adopt a validated stored state (boot only); config stays as constructed.
  void restore(const TraitLedger* traits, const JournalEntry* journal, uint8_t head,
               uint16_t sequence) {
    for (uint8_t t = 0; t < kTraitCount; ++t) traits_[t] = traits[t];
    for (uint8_t i = 0; i < kJournalSize; ++i) journal_[i] = journal[i];
    head_ = static_cast<uint8_t>(head % kJournalSize);
    sequence_ = sequence;
  }

 private:
  static int32_t clamp(int32_t value, int32_t range) {
    return value > range ? range : value < -range ? -range : value;
  }

  static void addWeight(uint16_t& slot, int32_t magnitude) {
    const uint32_t sum = uint32_t(slot) + uint32_t(magnitude >> 6) + 1;
    slot = sum > 0xffff ? 0xffff : static_cast<uint16_t>(sum);
  }

  static void closeSession(TraitLedger& l) {
    if (l.lastSession == 0) return;
    if (l.sessions < 0xff) ++l.sessions;
    if (l.sessionNet > 0 && l.positive < 0xff) ++l.positive;
    if (l.sessionNet < 0 && l.negative < 0xff) ++l.negative;
    l.sessionNet = 0;
    l.sessionMagnitude = 0;
  }

  static uint8_t dominant(const uint16_t* weights, uint8_t count) {
    uint8_t best = 0;
    for (uint8_t i = 1; i < count; ++i)
      if (weights[i] > weights[best]) best = i;
    return best;
  }

  void record(uint8_t t, const TraitLedger& l, int16_t applied, uint32_t sessions) {
    if (applied == 0) return;  // at the limit: nothing changed, nothing to explain
    JournalEntry& e = journal_[head_];
    e.sequence = ++sequence_ ? sequence_ : (sequence_ = 1);
    e.trait = t;
    e.reason = dominant(l.reasonWeight, kEvidenceReasonCount);
    e.source = dominant(l.sourceWeight, kEvidenceSourceCount);
    e.sessions = sessions > 0xff ? 0xff : static_cast<uint8_t>(sessions);
    e.delta = applied;
    head_ = static_cast<uint8_t>((head_ + 1) % kJournalSize);
  }

  LedgerConfig config_;
  TraitLedger traits_[kTraitCount];
  JournalEntry journal_[kJournalSize];
  uint8_t head_ = 0;
  uint16_t sequence_ = 0;
};

}  // namespace xuna
