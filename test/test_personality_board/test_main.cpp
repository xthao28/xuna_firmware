#include <unity.h>

#include <xuna/lab/BoardLab.h>
#include <xuna/personality/Expression.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

// Personality PE-2: LifeCore owns Personality on the board (shadow) and
// Expression maps it to animation pace and reaction length.

namespace {

void bootMigrated(BoardLab& lab) {
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  TEST_ASSERT_TRUE(lab.boot(0));
}

bool neutral(const TemperamentState& g) {
  for (uint8_t i = 0; i < kTraitCount; ++i)
    if (g.baseline[i] || g.offset[i]) return false;
  return true;
}

}  // namespace

void boot_seeds_the_genome_from_the_durable_identity() {
  BoardLab lab(7);
  bootMigrated(lab);
  TEST_ASSERT_TRUE(lab.lastBoot().identityReady);
  TemperamentState expected;
  Temperament::seed(expected, lab.identity().record().seed);
  const TemperamentState& g = lab.core().personality().genome();
  TEST_ASSERT_EQUAL_INT16_ARRAY(expected.baseline, g.baseline, kTraitCount);
  for (uint8_t i = 0; i < kTraitCount; ++i) TEST_ASSERT_EQUAL_INT16(0, g.offset[i]);
  // A reboot keeps who XUNA was born as (the seed is durable).
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_INT16_ARRAY(expected.baseline,
                                lab.core().personality().genome().baseline, kTraitCount);
}

void no_identity_means_a_neutral_genome_and_todays_timing() {
  LifeCore core;  // never seeded, like an Egg device
  TEST_ASSERT_TRUE(core.begin(0, true));
  TEST_ASSERT_TRUE(neutral(core.personality().genome()));
  const ExpressionStyle s = Expression::styleFor(core.personality());
  TEST_ASSERT_EQUAL_UINT16(100, s.framePeriodMs);
  TEST_ASSERT_EQUAL_UINT16(kHappyDurationMs, s.reactionMs);

  BoardLab egg(7);  // empty store: nothing to migrate, no identity yet
  egg.boot(0);
  TEST_ASSERT_FALSE(egg.lastBoot().identityReady);
  TEST_ASSERT_TRUE(neutral(egg.core().personality().genome()));
}

void xuna_alone_gives_no_evidence_owner_reactions_do() {
  BoardLab lab(7);
  bootMigrated(lab);
  uint32_t now = 0;
  uint32_t shown = 0;
  for (; now < 10 * 60000U; now += 1000) {  // ten quiet minutes, XUNA acts alone
    lab.loop(now);
    if (lab.adapter().last().decisionShown) ++shown;
  }
  TEST_ASSERT_TRUE(shown > 0);
  TEST_ASSERT_EQUAL_UINT32(0, lab.core().personality().sessions());
  TEST_ASSERT_EQUAL_UINT32(0, lab.core().personality().evidence());

  // When XUNA is idle again and no reaction is open, the owner presses GPIO0
  // (XUNA answers the greeting), then pets it.
  while (lab.core().reactionOpen(now) || lab.adapter().last().activity != Activity::Idle) {
    now += 1000;
    lab.loop(now);
  }
  TEST_ASSERT_TRUE(lab.greeting(now));
  TEST_ASSERT_TRUE(lab.greeting(now + 2000));
  TEST_ASSERT_TRUE(lab.adapter().lastReactionApplied());
  const Personality& p = lab.core().personality();
  TEST_ASSERT_EQUAL_UINT32(1, p.sessions());
  TEST_ASSERT_EQUAL_UINT32(1, p.evidence());
  TEST_ASSERT_TRUE(p.ledger().trait(TraitId::Sociability).pending > 0);
  TEST_ASSERT_TRUE(p.affect().state().emotionValence > 0);
}

void forget_clears_evidence_and_keeps_the_baseline() {
  BoardLab lab(7);
  bootMigrated(lab);
  TEST_ASSERT_TRUE(lab.greeting(1000));
  TEST_ASSERT_TRUE(lab.greeting(3000));
  TEST_ASSERT_TRUE(lab.core().personality().ledger().trait(TraitId::Sociability).importance > 0);
  const TemperamentState before = lab.core().personality().genome();
  uint8_t frame[16];
  TEST_ASSERT_EQUAL_UINT8(0, lab.command(frame, BoardLab::forget(frame, 9), 5000));
  const Personality& p = lab.core().personality();
  TEST_ASSERT_EQUAL_INT32(0, p.ledger().trait(TraitId::Sociability).importance);
  TEST_ASSERT_EQUAL_INT16_ARRAY(before.baseline, p.genome().baseline, kTraitCount);
}

void style_follows_the_real_trait_range_and_is_bounded() {
  TemperamentState g;
  AffectState a;
  g.offset[uint8_t(TraitId::Tempo)] = Expression::kFullEffect;  // lively 0.3
  TEST_ASSERT_EQUAL_UINT16(85, Expression::styleFor(g, a).framePeriodMs);
  g.offset[uint8_t(TraitId::Tempo)] = kOffsetRange;  // beyond 0.3: no more
  TEST_ASSERT_EQUAL_UINT16(85, Expression::styleFor(g, a).framePeriodMs);
  g.offset[uint8_t(TraitId::Tempo)] = -Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT16(115, Expression::styleFor(g, a).framePeriodMs);
  g.offset[uint8_t(TraitId::Tempo)] = 0;
  a.moodArousal = -Expression::kFullEffect;  // bored: half the effect
  TEST_ASSERT_EQUAL_UINT16(107, Expression::styleFor(g, a).framePeriodMs);

  a = AffectState();
  g.offset[uint8_t(TraitId::Sensitivity)] = Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT16(2760, Expression::styleFor(g, a).reactionMs);
  g.offset[uint8_t(TraitId::Sensitivity)] = -Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT16(2040, Expression::styleFor(g, a).reactionMs);
  // A strong emotion, either sign, lengthens the reaction.
  g.offset[uint8_t(TraitId::Sensitivity)] = 0;
  a.emotionValence = -2 * Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT16(2760, Expression::styleFor(g, a).reactionMs);
  a.emotionValence = 2 * Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT16(2760, Expression::styleFor(g, a).reactionMs);

  // Every seed stays inside ±15 % (innate range ±0.15 = half the effect).
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    TemperamentState s;
    Temperament::seed(s, seed);
    const ExpressionStyle e = Expression::styleFor(s, AffectState());
    TEST_ASSERT_TRUE(e.framePeriodMs >= 92 && e.framePeriodMs <= 108);
    TEST_ASSERT_TRUE(e.reactionMs >= 2220 && e.reactionMs <= 2580);
  }
}

void mood_words_follow_mood() {
  AffectState a;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Calm), uint8_t(Expression::moodWord(a)));
  a.moodValence = Expression::kGlad;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Glad), uint8_t(Expression::moodWord(a)));
  a.moodValence = Expression::kJoyful;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Joyful), uint8_t(Expression::moodWord(a)));
  a.moodValence = -Expression::kGlad;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Low), uint8_t(Expression::moodWord(a)));
  a.moodValence = Affect::kMoodFloor;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Sad), uint8_t(Expression::moodWord(a)));
  a.moodValence = 0;
  a.moodArousal = Expression::kBored;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Bored), uint8_t(Expression::moodWord(a)));
}

void spontaneous_wait_follows_temperament() {
  TemperamentState g;
  TEST_ASSERT_EQUAL_UINT32(60000, Expression::spontaneousWaitMs(g));  // today's routine
  g.offset[uint8_t(TraitId::Curiosity)] = Expression::kFullEffect;
  g.offset[uint8_t(TraitId::Playfulness)] = Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT32(51000, Expression::spontaneousWaitMs(g));  // eager
  g.offset[uint8_t(TraitId::Curiosity)] = kOffsetRange;              // beyond 0.3: no more
  TEST_ASSERT_EQUAL_UINT32(51000, Expression::spontaneousWaitMs(g));
  g.offset[uint8_t(TraitId::Curiosity)] = -Expression::kFullEffect;
  g.offset[uint8_t(TraitId::Playfulness)] = -Expression::kFullEffect;
  TEST_ASSERT_EQUAL_UINT32(69000, Expression::spontaneousWaitMs(g));  // placid
  for (uint32_t seed = 1; seed <= 200; ++seed) {  // innate ±0.15: half the effect at most
    TemperamentState s;
    Temperament::seed(s, seed);
    const uint32_t w = Expression::spontaneousWaitMs(s);
    TEST_ASSERT_TRUE(w >= 55500 && w <= 64500);
  }
  LocalBehavior routine;
  uint8_t action = 0;
  routine.activity(0);
  TEST_ASSERT_FALSE(routine.choosePreferred(50999, true, 1, action, 51000));
  TEST_ASSERT_TRUE(routine.choosePreferred(51000, true, 1, action, 51000));
  TEST_ASSERT_FALSE(routine.choosePreferred(51000 + 59999, true, 1, action));  // default 60 s
}

void board_routine_waits_by_the_pets_temperament() {
  BoardLab lab(7);
  bootMigrated(lab);
  const uint32_t wait = Expression::spontaneousWaitMs(lab.core().personality());
  TEST_ASSERT_TRUE(wait != 60000);  // this pet is not exactly neutral
  uint32_t first = 0;
  for (uint32_t now = 1000; now <= 80000 && !first; now += 1000) {
    lab.loop(now);
    if (lab.adapter().last().decisionShown) first = now;
  }
  TEST_ASSERT_EQUAL_UINT32((wait + 999) / 1000 * 1000, first);
}

void trait_word_names_the_most_pronounced_tendency() {
  TemperamentState g;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Balanced), uint8_t(Expression::traitWord(g)));
  g.offset[uint8_t(TraitId::Tempo)] = Expression::kPronounced - 1;  // below: still balanced
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Balanced), uint8_t(Expression::traitWord(g)));
  g.offset[uint8_t(TraitId::Tempo)] = -Expression::kPronounced;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Unhurried), uint8_t(Expression::traitWord(g)));
  g.offset[uint8_t(TraitId::Sociability)] = 2000;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Affectionate), uint8_t(Expression::traitWord(g)));
  g.offset[uint8_t(TraitId::Curiosity)] = -2000;  // tie: the earlier trait wins
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Composed), uint8_t(Expression::traitWord(g)));
  g.offset[uint8_t(TraitId::Boredom)] = 9000;     // not a shown trait
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Composed), uint8_t(Expression::traitWord(g)));

  BoardLab lab(7);  // the board pet: its innate label, from the durable seed
  bootMigrated(lab);
  TemperamentState innate;
  Temperament::seed(innate, lab.identity().record().seed);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Expression::traitWord(innate)),
                          uint8_t(Expression::traitWord(lab.core().personality().genome())));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(boot_seeds_the_genome_from_the_durable_identity);
  RUN_TEST(no_identity_means_a_neutral_genome_and_todays_timing);
  RUN_TEST(xuna_alone_gives_no_evidence_owner_reactions_do);
  RUN_TEST(forget_clears_evidence_and_keeps_the_baseline);
  RUN_TEST(style_follows_the_real_trait_range_and_is_bounded);
  RUN_TEST(mood_words_follow_mood);
  RUN_TEST(spontaneous_wait_follows_temperament);
  RUN_TEST(board_routine_waits_by_the_pets_temperament);
  RUN_TEST(trait_word_names_the_most_pronounced_tendency);
  return UNITY_END();
}
