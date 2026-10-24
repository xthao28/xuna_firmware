#include <unity.h>

#include <xuna/lab/BoardLab.h>
#include <xuna/personality/Leaning.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

// Personality PE-4a: the temperament leans XUNA's choices only where
// learning has no answer (Leaning.h, V3Leaning in PreferenceLearnerV3).

namespace {

const uint32_t kReaction = 19661;  // w_r = 0.3

V3Decision taught(uint32_t id, uint8_t interaction, uint8_t action) {
  V3Decision d;
  d.id = id;
  d.interaction = interaction;
  d.action = action;
  return d;
}

TemperamentState extreme(int16_t sign) {
  TemperamentState g;
  for (uint8_t t = 0; t < kTraitCount; ++t) {
    g.baseline[t] = static_cast<int16_t>(sign * kBaselineRange);
    g.offset[t] = static_cast<int16_t>(sign * kOffsetRange);
  }
  return g;
}

void bootFresh(BoardLab& lab) {
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  TEST_ASSERT_TRUE(lab.boot(0));
}

}  // namespace

void neutral_genome_chooses_like_learning_alone() {
  PreferenceLearnerV3 alone(productLearnerConfig());
  PreferenceLearnerV3 leaned(productLearnerConfig());
  Pcg32 ra(5, 9), rb(5, 9);
  LeaningConfig config;
  config.tieGainQ8 = 256;
  config.marginQ16 = 3932;  // a margin alone must not lean either
  const uint8_t masks[] = {0x7, 0x3, 0x5, 0x6, 0x7};
  for (uint32_t id = 1; id <= 900; ++id) {
    const uint8_t i = static_cast<uint8_t>(id % 3);
    const uint8_t d = static_cast<uint8_t>(id % 5);
    const V3Leaning lean =
        Leaning::forChoice(TemperamentState(), static_cast<LearnInteraction>(i), config);
    const V3Decision x = alone.choose(i, d, id, masks[id % 5], ra);
    const V3Decision y = leaned.choose(i, d, id, masks[id % 5], rb, lean);
    TEST_ASSERT_EQUAL_UINT8(x.action, y.action);
    TEST_ASSERT_EQUAL(x.explore, y.explore);
    if (id % 4 == 0) {
      const bool liked = (id / 4) % 3 != 0;
      TEST_ASSERT_TRUE(alone.feedback(x, liked, kReaction));
      TEST_ASSERT_TRUE(leaned.feedback(y, liked, kReaction));
    }
  }
  TEST_ASSERT_EQUAL_UINT32(alone.fingerprint(), leaned.fingerprint());
  TEST_ASSERT_EQUAL_UINT32(ra.next(), rb.next());  // same draws consumed
}

void untaught_cell_follows_the_lean() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  Pcg32 random(3, 1);
  V3Leaning lean;
  lean.weightQ8[2] = 512;  // curious twice as likely as idle or happy
  uint32_t n[3] = {};
  for (uint32_t id = 1; id <= 4000; ++id) ++n[learner.choose(0, 0, id, 0x7, random, lean).action];
  TEST_ASSERT_UINT32_WITHIN(150, 2000, n[2]);  // 512 / 1024
  TEST_ASSERT_UINT32_WITHIN(150, 1000, n[0]);
  TEST_ASSERT_UINT32_WITHIN(150, 1000, n[1]);
  // Only the allowed actions take part.
  uint32_t curious = 0;
  for (uint32_t id = 5001; id <= 5400; ++id)
    curious += learner.choose(0, 0, id, 0x3, random, lean).action == 2;
  TEST_ASSERT_EQUAL_UINT32(0, curious);
}

// Plan §5 "trait không đè learning": after three lessons the choice is the
// same whatever the genome, even the most extreme one.
void three_lessons_beat_any_temperament() {
  LeaningConfig strong;
  strong.tieGainQ8 = 256;
  strong.marginQ16 = 1966;
  const uint32_t weights[] = {65536U, kReaction};  // ratings, then reactions
  for (uint8_t action = 0; action < 3; ++action)
    for (uint32_t w : weights)
      for (int16_t sign = -1; sign <= 1; sign += 2)
        for (uint8_t i = 0; i < kLearnInteractionCount; ++i) {
          PreferenceLearnerV3 learner(productLearnerConfig());
          for (uint32_t k = 0; k < 3; ++k)
            TEST_ASSERT_TRUE(learner.feedback(taught(100 + k, i, action), true, w));
          const V3Leaning lean =
              Leaning::forChoice(extreme(sign), static_cast<LearnInteraction>(i), strong);
          Pcg32 random(11, 2);
          for (uint32_t id = 1; id <= 200; ++id)
            TEST_ASSERT_EQUAL_UINT8(action, learner.choose(i, 0, 1000 + id, 0x7, random, lean).action);
        }
}

// One reaction for idle puts its UCB score 0.047 above the untried actions.
void margin_decides_only_near_ties() {
  PreferenceLearnerV3 learner(productLearnerConfig());
  TEST_ASSERT_TRUE(learner.feedback(taught(1, 0, 0), true, kReaction));
  V3Leaning away;  // leans away from idle
  away.weightQ8[0] = 32;
  away.weightQ8[2] = 512;
  Pcg32 random(4, 4);
  uint32_t other[3] = {};
  const uint32_t margins[] = {1966, 3932, 3932};  // 0.03, 0.06, 0.06 with equal weights
  for (uint8_t k = 0; k < 3; ++k) {
    V3Leaning lean = k == 2 ? V3Leaning() : away;
    lean.marginQ16 = margins[k];
    for (uint32_t id = 1; id <= 300; ++id)
      other[k] += learner.choose(0, 0, 10 * id + k, 0x7, random, lean).action != 0;
  }
  TEST_ASSERT_EQUAL_UINT32(0, other[0]);  // 0.047 is more than a 0.03 margin
  TEST_ASSERT_TRUE(other[1] > 150);       // within 0.06: the lean decides
  TEST_ASSERT_EQUAL_UINT32(0, other[2]);  // no lean, no near-tie
}

void lean_maps_traits_to_actions() {
  LeaningConfig config;  // board default: weight 1 ± 1 at |trait| 0.3
  const V3Leaning neutral = Leaning::forChoice(TemperamentState(), LearnInteraction::QuietRequest, config);
  for (uint8_t a = 0; a < 3; ++a) TEST_ASSERT_EQUAL_UINT16(256, neutral.weightQ8[a]);
  TEST_ASSERT_EQUAL_UINT32(0, neutral.marginQ16);

  TemperamentState g;
  g.offset[uint8_t(TraitId::Curiosity)] = 4915;     // 0.3: full effect
  g.offset[uint8_t(TraitId::Tempo)] = 4915;         // pace only: idle keeps 1
  g.offset[uint8_t(TraitId::Playfulness)] = -2458;  // −0.15: half effect
  g.offset[uint8_t(TraitId::Sociability)] = 9830;   // 0.6: capped at full effect
  const V3Leaning quiet = Leaning::forChoice(g, LearnInteraction::QuietRequest, config);
  TEST_ASSERT_EQUAL_UINT16(256, quiet.weightQ8[0]);
  TEST_ASSERT_EQUAL_UINT16(128, quiet.weightQ8[1]);
  TEST_ASSERT_EQUAL_UINT16(512, quiet.weightQ8[2]);
  // When greeted, the warm answer follows sociability instead of playfulness.
  const V3Leaning greeting = Leaning::forChoice(g, LearnInteraction::ButtonGreeting, config);
  TEST_ASSERT_EQUAL_UINT16(512, greeting.weightQ8[1]);

  g.offset[uint8_t(TraitId::Curiosity)] = -9830;  // the least curious pet
  const V3Leaning composed = Leaning::forChoice(g, LearnInteraction::QuietRequest, config);
  TEST_ASSERT_EQUAL_UINT16(Leaning::kMinWeightQ8, composed.weightQ8[2]);  // never ruled out

  LeaningConfig mild;
  mild.tieGainQ8 = 128;
  const V3Leaning half = Leaning::forChoice(g, LearnInteraction::QuietRequest, mild);
  TEST_ASSERT_EQUAL_UINT16(192, half.weightQ8[1]);
  TEST_ASSERT_EQUAL_UINT16(128, half.weightQ8[2]);
}

// The board leans its own untaught choices; with no gain it is
// learning alone.
void board_leans_its_untaught_choices() {
  uint32_t seed = 0, bestShare = 0;
  // The most curious-born of a few board pets (PetId entropy differs).
  for (uint32_t e = 1; e <= 40; ++e) {
    BoardLab lab(e);
    bootFresh(lab);
    const V3Leaning l = Leaning::forChoice(lab.core().personality().genome(),
                                           LearnInteraction::QuietRequest, LeaningConfig());
    const uint32_t share =
        1000U * l.weightQ8[2] / (l.weightQ8[0] + l.weightQ8[1] + l.weightQ8[2]);
    if (share > bestShare) {
      bestShare = share;
      seed = e;
    }
  }
  TEST_ASSERT_GREATER_THAN_UINT32(400, bestShare);
  // One day alone on the board's quiet routine; nobody reacts, so every
  // cell stays untaught.
  uint32_t curious[2] = {}, shown[2] = {};
  for (uint8_t k = 0; k < 2; ++k) {
    LeaningConfig config;
    if (k == 0) config.tieGainQ8 = 0;
    BoardLab lab(seed);
    lab.setLeaning(config);
    bootFresh(lab);
    for (uint32_t now = 1000; now < 86400000U; now += 1000) {
      lab.loop(now);
      const LifeCoreStepOutput& out = lab.adapter().last();
      if (!out.decisionShown) continue;
      ++shown[k];
      curious[k] += out.selectedAction == uint8_t(ActionId::Curious);
    }
  }
  TEST_ASSERT_GREATER_THAN_UINT32(300, shown[0]);
  // Shares in per mille: no lean ≈ 1/3, the curious-born pet clearly more.
  const uint32_t without = 1000U * curious[0] / shown[0];
  const uint32_t with = 1000U * curious[1] / shown[1];
  TEST_ASSERT_GREATER_THAN_UINT32(without + 50, with);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(neutral_genome_chooses_like_learning_alone);
  RUN_TEST(untaught_cell_follows_the_lean);
  RUN_TEST(three_lessons_beat_any_temperament);
  RUN_TEST(margin_decides_only_near_ties);
  RUN_TEST(lean_maps_traits_to_actions);
  RUN_TEST(board_leans_its_untaught_choices);
  return UNITY_END();
}
