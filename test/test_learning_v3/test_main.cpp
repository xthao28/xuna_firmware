#include <unity.h>
#include <xuna/learning/PreferenceLearnerV3.h>

using namespace xuna;

namespace {

LearnerV3Config config(ExplorePolicy policy = ExplorePolicy::EpsilonGreedy) {
  LearnerV3Config c;
  c.interactions = 2;
  c.dayParts = 5;
  c.actions = 3;
  c.policy = policy;
  return c;
}

V3Decision shown(uint8_t interaction, uint8_t dayPart, uint8_t action,
                 uint32_t id) {
  V3Decision d;
  d.id = id;
  d.interaction = interaction;
  d.dayPart = dayPart;
  d.action = action;
  return d;
}

}  // namespace

void discount_applies_only_to_rated_cell() {
  PreferenceLearnerV3 learner(config());
  TEST_ASSERT_TRUE(learner.feedback(shown(0, 0, 1, 1), true));
  TEST_ASSERT_EQUAL_UINT32(65536, learner.view(0, 0, 1).positiveQ16);
  TEST_ASSERT_TRUE(learner.feedback(shown(0, 0, 1, 2), true));
  // 65536 * 0.95 (62259/65536) + 65536
  TEST_ASSERT_EQUAL_UINT32(62259U + 65536U, learner.view(0, 0, 1).positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(0, learner.view(0, 0, 0).nEffQ16);
  TEST_ASSERT_EQUAL_UINT32(0, learner.view(1, 0, 1).nEffQ16);
}

void silence_never_changes_state() {
  PreferenceLearnerV3 learner(config());
  learner.feedback(shown(0, 0, 2, 1), true);
  const uint32_t before = learner.fingerprint();
  Pcg32 random(9, 3);
  for (uint32_t id = 10; id < 500; ++id)
    learner.choose(static_cast<uint8_t>(id % 2), 0, id, 0x07, random);
  TEST_ASSERT_EQUAL_UINT32(before, learner.fingerprint());
}

void evidence_mass_is_bounded_by_gamma() {
  PreferenceLearnerV3 learner(config());
  for (uint32_t id = 1; id <= 400; ++id)
    learner.feedback(shown(0, 0, 0, id), true);
  // Effective memory 1/(1-γ) = 20 ratings.
  const uint32_t mass = learner.view(0, 0, 0).nEffQ16;
  TEST_ASSERT_TRUE(mass <= 20U * 65536U + 65536U);
  TEST_ASSERT_TRUE(mass >= 19U * 65536U);
}

void reversal_recovers_within_a_few_ratings() {
  LearnerV3Config c = config(ExplorePolicy::Greedy);
  c.actions = 2;
  PreferenceLearnerV3 learner(c);
  uint32_t id = 1;
  for (uint32_t i = 0; i < 40; ++i) {
    learner.feedback(shown(0, 0, 1, id++), true);
    learner.feedback(shown(0, 0, 0, id++), false);
  }
  TEST_ASSERT_EQUAL_UINT8(1, learner.greedy(0, 0));
  uint32_t ratings = 0;
  while (learner.greedy(0, 0) == 1 && ratings < 100) {
    learner.feedback(shown(0, 0, 1, id++), false);
    learner.feedback(shown(0, 0, 0, id++), true);
    ratings += 2;
  }
  TEST_ASSERT_EQUAL_UINT8(0, learner.greedy(0, 0));
  TEST_ASSERT_TRUE(ratings <= 30);
}

void fine_cell_needs_evidence_then_takes_over() {
  PreferenceLearnerV3 learner(config());
  uint32_t id = 1;
  // Coarse preference for action 0 in interaction 0.
  for (uint32_t i = 0; i < 10; ++i) learner.feedback(shown(0, 0, 0, id++), true);
  // A new day part starts from the coarse estimate.
  TEST_ASSERT_FALSE(learner.view(0, 3, 0).fine);
  TEST_ASSERT_EQUAL_UINT16(learner.view(0, 0, 0).pQ16,
                           learner.view(0, 3, 0).pQ16);
  // Evening (day part 3) prefers action 2.
  for (uint32_t i = 0; i < 6; ++i) {
    learner.feedback(shown(0, 3, 2, id++), true);
    learner.feedback(shown(0, 3, 0, id++), false);
  }
  TEST_ASSERT_TRUE(learner.view(0, 3, 2).fine);
  TEST_ASSERT_EQUAL_UINT8(2, learner.greedy(0, 3));
  // Other interactions are isolated.
  TEST_ASSERT_EQUAL_UINT32(0, learner.view(1, 3, 2).nEffQ16);
  TEST_ASSERT_EQUAL_UINT32(0, learner.view(1, 0, 0).nEffQ16);
}

void feedback_rejects_duplicates_and_invalid() {
  PreferenceLearnerV3 learner(config());
  TEST_ASSERT_TRUE(learner.feedback(shown(0, 0, 1, 5), true));
  const uint32_t after = learner.fingerprint();
  TEST_ASSERT_FALSE(learner.feedback(shown(0, 0, 1, 5), true));
  TEST_ASSERT_FALSE(learner.feedback(shown(0, 0, 1, 0), true));
  TEST_ASSERT_FALSE(learner.feedback(shown(0, 0, 3, 6), true));
  TEST_ASSERT_FALSE(learner.feedback(shown(2, 0, 1, 7), true));
  TEST_ASSERT_FALSE(learner.feedback(shown(0, 5, 1, 8), true));
  TEST_ASSERT_EQUAL_UINT32(after, learner.fingerprint());
}

void choose_respects_allowed_mask_and_is_deterministic() {
  PreferenceLearnerV3 a(config(ExplorePolicy::EpsilonGreedy));
  PreferenceLearnerV3 b(config(ExplorePolicy::EpsilonGreedy));
  Pcg32 ra(4, 3), rb(4, 3);
  for (uint32_t id = 1; id < 300; ++id) {
    const V3Decision x = a.choose(0, 0, id, 0x05, ra);
    const V3Decision y = b.choose(0, 0, id, 0x05, rb);
    TEST_ASSERT_TRUE(x.action == 0 || x.action == 2);
    TEST_ASSERT_EQUAL_UINT8(x.action, y.action);
    TEST_ASSERT_EQUAL(x.explore, y.explore);
  }
  TEST_ASSERT_EQUAL_UINT32(0, a.choose(0, 0, 1, 0x00, ra).id);
  TEST_ASSERT_EQUAL_UINT32(0, a.choose(0, 0, 0, 0x07, ra).id);
}

void epsilon_shrinks_with_confidence() {
  PreferenceLearnerV3 learner(config(ExplorePolicy::EpsilonGreedy));
  TEST_ASSERT_EQUAL_UINT32(19661, learner.epsilonQ16(0, 0));
  for (uint32_t id = 1; id <= 30; ++id)
    learner.feedback(shown(0, 0, 0, id), true);
  const uint32_t later = learner.epsilonQ16(0, 0);
  TEST_ASSERT_TRUE(later < 19661 / 2);
  TEST_ASSERT_TRUE(later >= 1966);
  PreferenceLearnerV3 greedy(config(ExplorePolicy::Greedy));
  TEST_ASSERT_EQUAL_UINT32(0, greedy.epsilonQ16(0, 0));
}

void learning_progress_reacts_to_surprise() {
  PreferenceLearnerV3 learner(config(ExplorePolicy::LearningProgress));
  Pcg32 random(1, 3);
  uint32_t id = 1;
  // Stable world: predictions become accurate, progress settles.
  for (uint32_t i = 0; i < 80; ++i) {
    V3Decision d = learner.choose(0, 0, id++, 0x01, random);
    learner.feedback(d, true);
  }
  const uint32_t settled = learner.epsilonQ16(0, 0);
  // Surprise: the same action is now rejected repeatedly.
  for (uint32_t i = 0; i < 4; ++i) {
    V3Decision d = learner.choose(0, 0, id++, 0x01, random);
    learner.feedback(d, false);
  }
  TEST_ASSERT_TRUE(learner.epsilonQ16(0, 0) > settled);
}

void ucb_tries_unrated_actions() {
  LearnerV3Config c = config(ExplorePolicy::Ucb);
  PreferenceLearnerV3 learner(c);
  // Mixed evidence: p = 0.5 with a smaller bonus than unrated actions.
  learner.feedback(shown(0, 0, 0, 1), true);
  learner.feedback(shown(0, 0, 0, 2), false);
  Pcg32 random(2, 3);
  const V3Decision d = learner.choose(0, 0, 100, 0x07, random);
  // Unrated actions 1 and 2 tie; choose() breaks the tie by RNG (§9.4).
  TEST_ASSERT_TRUE(d.action == 1 || d.action == 2);
}

void feedback_aware_exploration_fades_without_ratings() {
  LearnerV3Config c = config(ExplorePolicy::EpsilonGreedy);
  c.feedbackAware = true;
  PreferenceLearnerV3 learner(c);
  Pcg32 random(3, 3);
  const uint32_t before = learner.epsilonQ16(0, 0);
  const uint32_t fingerprint = learner.fingerprint();
  for (uint32_t id = 1; id <= 40; ++id) learner.choose(0, 0, id, 0x07, random);
  TEST_ASSERT_TRUE(learner.epsilonQ16(0, 0) < before / 4);
  // Exploration bookkeeping is not learned state.
  TEST_ASSERT_EQUAL_UINT32(fingerprint, learner.fingerprint());
  // Ratings bring exploration back.
  for (uint32_t id = 100; id < 140; ++id) {
    const V3Decision d = learner.choose(0, 0, id, 0x07, random);
    learner.feedback(d, d.action == 0);
  }
  TEST_ASSERT_TRUE(learner.epsilonQ16(0, 0) > 0);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(discount_applies_only_to_rated_cell);
  RUN_TEST(silence_never_changes_state);
  RUN_TEST(evidence_mass_is_bounded_by_gamma);
  RUN_TEST(reversal_recovers_within_a_few_ratings);
  RUN_TEST(fine_cell_needs_evidence_then_takes_over);
  RUN_TEST(feedback_rejects_duplicates_and_invalid);
  RUN_TEST(choose_respects_allowed_mask_and_is_deterministic);
  RUN_TEST(epsilon_shrinks_with_confidence);
  RUN_TEST(learning_progress_reacts_to_surprise);
  RUN_TEST(ucb_tries_unrated_actions);
  RUN_TEST(feedback_aware_exploration_fades_without_ratings);
  return UNITY_END();
}
