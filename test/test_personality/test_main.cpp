#include <unity.h>

#include <xuna/personality/EvidenceLedger.h>
#include <xuna/personality/Temperament.h>

using namespace xuna;

namespace {

const int16_t kUp = kTraitOne;
const int16_t kDown = -kTraitOne;

// `count` pets (or shoos) in one session for one trait.
void reactions(EvidenceLedger& ledger, TraitId trait, int16_t value, uint32_t session,
               uint32_t count = 5) {
  for (uint32_t i = 0; i < count; ++i)
    ledger.observe(trait, value, EvidenceSource::OwnerReaction,
                   value > 0 ? EvidenceReason::ExploreRewarded : EvidenceReason::ExploreShooed,
                   session);
}

}  // namespace

void seed_is_deterministic_bounded_and_individual() {
  TemperamentState a, b, c;
  Temperament::seed(a, 0x5eed1234U);
  Temperament::seed(b, 0x5eed1234U);
  Temperament::seed(c, 0x5eed1235U);
  // Golden: the genome must stay the same on every platform and release.
  const int16_t golden[kTraitCount] = {1193, 2297, 2079, 2332, 2320, 1415, 449, 1088, 2111, -2342};
  TEST_ASSERT_EQUAL_INT16_ARRAY(golden, a.baseline, kTraitCount);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.baseline, b.baseline, kTraitCount);
  bool differs = false;
  for (uint8_t i = 0; i < kTraitCount; ++i) {
    differs = differs || a.baseline[i] != c.baseline[i];
    TEST_ASSERT_EQUAL_INT16(0, a.offset[i]);
  }
  TEST_ASSERT_TRUE(differs);

  // Over many pets every trait spreads across ±0.15 around 0.
  int32_t sum[kTraitCount] = {};
  int16_t low[kTraitCount] = {}, high[kTraitCount] = {};
  for (uint32_t seed = 1; seed <= 400; ++seed) {
    TemperamentState s;
    Temperament::seed(s, seed * 2654435761U);
    for (uint8_t i = 0; i < kTraitCount; ++i) {
      TEST_ASSERT_TRUE(s.baseline[i] >= -kBaselineRange && s.baseline[i] <= kBaselineRange);
      sum[i] += s.baseline[i];
      if (s.baseline[i] < low[i]) low[i] = s.baseline[i];
      if (s.baseline[i] > high[i]) high[i] = s.baseline[i];
    }
  }
  for (uint8_t i = 0; i < kTraitCount; ++i) {
    TEST_ASSERT_TRUE(sum[i] / 400 > -300 && sum[i] / 400 < 300);
    TEST_ASSERT_TRUE(low[i] < -2000 && high[i] > 2000);
  }
}

void effective_shift_and_forget_respect_ranges() {
  TemperamentState s;
  Temperament::seed(s, 0x5eed1234U);  // curiosity baseline 1193
  TEST_ASSERT_EQUAL_INT16(1193, Temperament::effective(s, TraitId::Curiosity));
  TEST_ASSERT_EQUAL_INT16(492, Temperament::shift(s, TraitId::Curiosity, 492));
  TEST_ASSERT_EQUAL_INT16(1685, Temperament::effective(s, TraitId::Curiosity));
  // The learned part saturates at ±0.6; the applied delta says so.
  TEST_ASSERT_EQUAL_INT16(kOffsetRange - 492,
                          Temperament::shift(s, TraitId::Curiosity, 32000));
  TEST_ASSERT_EQUAL_INT16(0, Temperament::shift(s, TraitId::Curiosity, 1));
  TEST_ASSERT_EQUAL_INT16(1193 + kOffsetRange, Temperament::effective(s, TraitId::Curiosity));
  Temperament::shift(s, TraitId::Restfulness, -32000);
  TEST_ASSERT_EQUAL_INT16(-2342 - kOffsetRange, Temperament::effective(s, TraitId::Restfulness));
  // ForgetAll keeps who it was born as.
  Temperament::forget(s);
  TEST_ASSERT_EQUAL_INT16(1193, Temperament::effective(s, TraitId::Curiosity));
  TEST_ASSERT_EQUAL_INT16(-2342, Temperament::effective(s, TraitId::Restfulness));
  TEST_ASSERT_EQUAL_INT16(0, Temperament::effective(s, static_cast<TraitId>(kTraitCount)));
}

void source_weights_follow_the_learner_and_self_is_ignored() {
  TEST_ASSERT_EQUAL_INT16(16384, EvidenceLedger::weight(EvidenceSource::OwnerRating));
  TEST_ASSERT_EQUAL_INT16(8192, EvidenceLedger::weight(EvidenceSource::OwnerStated));
  TEST_ASSERT_EQUAL_INT16(4915, EvidenceLedger::weight(EvidenceSource::OwnerReaction));
  TEST_ASSERT_TRUE(EvidenceLedger::weight(EvidenceSource::ConversationInferred) <=
                   EvidenceLedger::weight(EvidenceSource::DeviceObserved));
  TEST_ASSERT_EQUAL_INT16(0, EvidenceLedger::weight(EvidenceSource::Self));

  EvidenceLedger ledger;
  TemperamentState s;
  for (uint32_t session = 1; session <= 50; ++session)
    for (uint32_t i = 0; i < 20; ++i)
      TEST_ASSERT_FALSE(ledger.observe(TraitId::Curiosity, kUp, EvidenceSource::Self,
                                       EvidenceReason::ExploreRewarded, session));
  TEST_ASSERT_FALSE(ledger.observe(TraitId::Curiosity, kUp, EvidenceSource::OwnerReaction,
                                   EvidenceReason::ExploreRewarded, 0));  // no session
  TEST_ASSERT_EQUAL_INT32(0, ledger.trait(TraitId::Curiosity).importance);
  TEST_ASSERT_EQUAL_UINT8(0, ledger.consolidate(s).shifted);
  TEST_ASSERT_EQUAL_INT16(0, s.offset[0]);
}

void one_extreme_session_never_moves_a_trait() {
  EvidenceLedger ledger;
  TemperamentState s;
  reactions(ledger, TraitId::Curiosity, kDown, 1, 1000);
  const TraitLedger& l = ledger.trait(TraitId::Curiosity);
  TEST_ASSERT_EQUAL_INT32(-EvidenceLedger::kMaxSessionShare, l.pending);
  TEST_ASSERT_EQUAL_INT32(EvidenceLedger::kMaxSessionShare, l.importance);
  for (uint32_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_UINT8(0, ledger.consolidate(s).shifted);
  TEST_ASSERT_EQUAL_INT16(0, s.offset[0]);
  // Two strong sessions are still not enough (≥ 3 sessions).
  reactions(ledger, TraitId::Curiosity, kDown, 2, 1000);
  TEST_ASSERT_EQUAL_UINT8(0, ledger.consolidate(s).shifted);
  TEST_ASSERT_EQUAL_INT16(0, s.offset[0]);
}

void bad_day_then_good_session_does_not_shift() {
  // Two sessions of shoos, then the owner is happy again: incoherent → decay.
  EvidenceLedger ledger;
  TemperamentState s;
  reactions(ledger, TraitId::Curiosity, kDown, 1);
  reactions(ledger, TraitId::Curiosity, kDown, 2);
  reactions(ledger, TraitId::Curiosity, kUp, 3);
  const ConsolidationReport r = ledger.consolidate(s);
  TEST_ASSERT_EQUAL_UINT8(0, r.shifted);
  TEST_ASSERT_EQUAL_UINT8(1, r.decayed);
  TEST_ASSERT_EQUAL_INT16(0, s.offset[0]);
  TEST_ASSERT_EQUAL_INT32(-8192 / 5, ledger.trait(TraitId::Curiosity).pending);
  TEST_ASSERT_EQUAL_UINT8(0, ledger.trait(TraitId::Curiosity).sessions);
}

void consistent_sessions_move_one_bounded_step_with_a_reason() {
  EvidenceLedger ledger;
  TemperamentState s;
  reactions(ledger, TraitId::Curiosity, kUp, 10);
  TEST_ASSERT_EQUAL_UINT8(0, ledger.consolidate(s).shifted);
  reactions(ledger, TraitId::Curiosity, kUp, 11);
  TEST_ASSERT_EQUAL_UINT8(0, ledger.consolidate(s).shifted);
  reactions(ledger, TraitId::Curiosity, kUp, 12);
  TEST_ASSERT_EQUAL_UINT8(1, ledger.consolidate(s).shifted);
  TEST_ASSERT_EQUAL_INT16(EvidenceLedger::kMaxStep, s.offset[0]);
  for (uint8_t i = 1; i < kTraitCount; ++i) TEST_ASSERT_EQUAL_INT16(0, s.offset[i]);
  const JournalEntry& e = ledger.journal(0);
  TEST_ASSERT_EQUAL_UINT16(1, e.sequence);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitId::Curiosity), e.trait);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EvidenceReason::ExploreRewarded), e.reason);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EvidenceSource::OwnerReaction), e.source);
  TEST_ASSERT_EQUAL_UINT8(3, e.sessions);
  TEST_ASSERT_EQUAL_INT16(EvidenceLedger::kMaxStep, e.delta);
  // The trait's ledger restarts from nothing.
  TEST_ASSERT_EQUAL_INT32(0, ledger.trait(TraitId::Curiosity).importance);
  TEST_ASSERT_EQUAL_UINT8(0, ledger.consolidate(s).shifted);
}

void alternating_owner_never_accumulates() {
  EvidenceLedger ledger;
  TemperamentState s;
  for (uint32_t session = 1; session <= 90; ++session) {
    reactions(ledger, TraitId::Sociability, session % 2 ? kUp : kDown, session);
    ledger.consolidate(s);
  }
  TEST_ASSERT_EQUAL_INT16(0, s.offset[uint8_t(TraitId::Sociability)]);
  TEST_ASSERT_EQUAL_UINT16(0, ledger.journal(0).sequence);
}

void steps_are_rate_limited_and_saturate() {
  EvidenceLedger ledger;
  TemperamentState s;
  uint32_t shifts = 0;
  for (uint32_t session = 1; session <= 300; ++session) {
    reactions(ledger, TraitId::Tempo, kDown, session);
    const int16_t before = s.offset[uint8_t(TraitId::Tempo)];
    shifts += ledger.consolidate(s).shifted;
    const int16_t step = int16_t(before - s.offset[uint8_t(TraitId::Tempo)]);
    TEST_ASSERT_TRUE(step >= 0 && step <= EvidenceLedger::kMaxStep);
  }
  TEST_ASSERT_EQUAL_INT16(-kOffsetRange, s.offset[uint8_t(TraitId::Tempo)]);
  // 0.6 / 0.03 = 20 steps of 3 sessions each: weeks, not a day.
  TEST_ASSERT_TRUE(shifts >= 20);
  TEST_ASSERT_EQUAL_INT16(-(kOffsetRange - 19 * EvidenceLedger::kMaxStep), ledger.journal(0).delta);
  TEST_ASSERT_EQUAL_UINT16(20, ledger.journal(0).sequence);  // limit hit: no more entries
}

void weak_device_evidence_needs_many_sessions() {
  EvidenceLedger ledger;
  TemperamentState s;
  uint32_t session = 0;
  while (s.offset[uint8_t(TraitId::Tempo)] == 0 && session < 100) {
    ++session;
    for (uint32_t i = 0; i < 2; ++i)
      ledger.observe(TraitId::Tempo, kUp, EvidenceSource::DeviceObserved,
                     EvidenceReason::LivelySession, session);
    ledger.consolidate(s);
  }
  // 2 × 0.1 per session: importance 1.5 is first reached in session 8.
  TEST_ASSERT_EQUAL_UINT32(8, session);
  TEST_ASSERT_EQUAL_INT16(EvidenceLedger::kMaxStep, s.offset[uint8_t(TraitId::Tempo)]);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EvidenceSource::DeviceObserved), ledger.journal(0).source);
  TEST_ASSERT_EQUAL_UINT8(8, ledger.journal(0).sessions);
}

void journal_keeps_the_latest_four_and_forget_clears() {
  EvidenceLedger ledger;
  TemperamentState s;
  uint32_t session = 0;
  for (uint8_t trait = 0; trait < 6; ++trait)
    for (uint8_t k = 0; k < 3; ++k) {
      reactions(ledger, static_cast<TraitId>(trait), kUp, ++session);
      ledger.consolidate(s);
    }
  for (uint8_t age = 0; age < EvidenceLedger::kJournalSize; ++age) {
    TEST_ASSERT_EQUAL_UINT16(6 - age, ledger.journal(age).sequence);
    TEST_ASSERT_EQUAL_UINT8(5 - age, ledger.journal(age).trait);
  }
  ledger.forget();
  Temperament::forget(s);
  TEST_ASSERT_EQUAL_UINT16(0, ledger.journal(0).sequence);
  for (uint8_t i = 0; i < kTraitCount; ++i) TEST_ASSERT_EQUAL_INT16(0, s.offset[i]);
}

void golden_history() {
  // 12 sessions: curiosity mostly rewarded, greeting petted 2 of 3 times,
  // weak device tempo evidence. Pins the whole arithmetic.
  TemperamentState s;
  Temperament::seed(s, 0x5eed1234U);
  EvidenceLedger l;
  for (uint32_t session = 1; session <= 12; ++session) {
    for (int k = 0; k < 4; ++k)
      l.observe(TraitId::Curiosity, kUp, EvidenceSource::OwnerReaction,
                EvidenceReason::ExploreRewarded, session);
    if (session % 4 == 0)
      l.observe(TraitId::Curiosity, kDown, EvidenceSource::OwnerReaction,
                EvidenceReason::ExploreShooed, session);
    l.observe(TraitId::Sociability, session % 3 ? kUp : kDown, EvidenceSource::OwnerReaction,
              EvidenceReason::GreetingPetted, session);
    l.observe(TraitId::Tempo, 8192, EvidenceSource::DeviceObserved,
              EvidenceReason::LivelySession, session);
    l.consolidate(s);
  }
  const int16_t offsets[kTraitCount] = {1968, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_INT16_ARRAY(offsets, s.offset, kTraitCount);
  // Sociability (2/3 positive, coherence 1/3) decayed twice and never moved.
  TEST_ASSERT_EQUAL_INT32(2359, l.trait(TraitId::Sociability).pending);
  TEST_ASSERT_EQUAL_INT32(14941, l.trait(TraitId::Sociability).importance);
  TEST_ASSERT_EQUAL_UINT8(1, l.trait(TraitId::Sociability).sessions);
  TEST_ASSERT_EQUAL_UINT16(4, l.journal(0).sequence);
  TEST_ASSERT_EQUAL_INT16(492, l.journal(3).delta);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(seed_is_deterministic_bounded_and_individual);
  RUN_TEST(effective_shift_and_forget_respect_ranges);
  RUN_TEST(source_weights_follow_the_learner_and_self_is_ignored);
  RUN_TEST(one_extreme_session_never_moves_a_trait);
  RUN_TEST(bad_day_then_good_session_does_not_shift);
  RUN_TEST(consistent_sessions_move_one_bounded_step_with_a_reason);
  RUN_TEST(alternating_owner_never_accumulates);
  RUN_TEST(steps_are_rate_limited_and_saturate);
  RUN_TEST(weak_device_evidence_needs_many_sessions);
  RUN_TEST(journal_keeps_the_latest_four_and_forget_clears);
  RUN_TEST(golden_history);
  return UNITY_END();
}
