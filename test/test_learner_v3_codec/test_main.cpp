#include <unity.h>
#include <xuna/learning/LearnerV3Codec.h>
#include <xuna/learning/LearnerV3Explanation.h>
#include <xuna/learning/LearnerVocabulary.h>

using namespace xuna;

namespace {

LearnerV3Config productShape(uint8_t actions = 4) {
  LearnerV3Config c;
  c.interactions = 2;
  c.dayParts = 5;
  c.actions = actions;
  c.policy = ExplorePolicy::Ucb;
  c.gammaQ16 = 58982;  // 0.90
  c.priorStrengthQ16 = 65536;
  c.fineMinQ16 = 65536;
  c.ucbBonusQ16 = 9830;
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

void train(PreferenceLearnerV3& learner) {
  uint32_t id = 1;
  for (uint32_t i = 0; i < 12; ++i) {
    learner.feedback(shown(0, 0, 1, id++), true);
    learner.feedback(shown(0, 3, 2, id++), i % 3 != 0);
    learner.feedback(shown(1, 1, 0, id++), false);
  }
}

}  // namespace

void encoded_size_matches_shape() {
  PreferenceLearnerV3 learner(productShape());
  TEST_ASSERT_EQUAL_UINT32(408, LearnerV3Codec::encodedSize(productShape()));
  uint8_t small[100] = {};
  TEST_ASSERT_EQUAL_UINT32(0, LearnerV3Codec::encode(learner, small, sizeof(small)));
  uint8_t buffer[408] = {};
  TEST_ASSERT_EQUAL_UINT32(408, LearnerV3Codec::encode(learner, buffer, sizeof(buffer)));
}

void round_trip_is_bit_exact() {
  PreferenceLearnerV3 original(productShape());
  train(original);
  uint8_t buffer[408] = {};
  const size_t size = LearnerV3Codec::encode(original, buffer, sizeof(buffer));

  PreferenceLearnerV3 restored(productShape());
  const LearnerDecodeResult result =
      LearnerV3Codec::decode(buffer, size, restored);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT32(original.fingerprint(), restored.fingerprint());
  TEST_ASSERT_EQUAL_UINT32(original.policyVersion(), restored.policyVersion());

  Pcg32 a(5, 3), b(5, 3);
  for (uint32_t id = 1000; id < 1100; ++id) {
    const uint8_t i = static_cast<uint8_t>(id % 2);
    const uint8_t d = static_cast<uint8_t>(id % 5);
    TEST_ASSERT_EQUAL_UINT8(original.choose(i, d, id, 0x0F, a).action,
                            restored.choose(i, d, id, 0x0F, b).action);
  }
}

void decode_rejects_corrupt_payloads_without_partial_state() {
  PreferenceLearnerV3 source(productShape());
  train(source);
  uint8_t buffer[408] = {};
  const size_t size = LearnerV3Codec::encode(source, buffer, sizeof(buffer));
  const uint32_t fresh = PreferenceLearnerV3(productShape()).fingerprint();

  PreferenceLearnerV3 target(productShape());
  TEST_ASSERT_EQUAL(LearnerDecodeError::BadSize,
                    LearnerV3Codec::decode(buffer, 4, target).error);
  TEST_ASSERT_EQUAL(LearnerDecodeError::BadSize,
                    LearnerV3Codec::decode(buffer, size - 1, target).error);
  TEST_ASSERT_EQUAL(LearnerDecodeError::BadSize,
                    LearnerV3Codec::decode(0, size, target).error);

  uint8_t bad[408];
  for (size_t i = 0; i < size; ++i) bad[i] = buffer[i];
  bad[2] = 9;
  TEST_ASSERT_EQUAL(LearnerDecodeError::BadShape,
                    LearnerV3Codec::decode(bad, size, target).error);
  bad[2] = buffer[2];
  bad[3] = 1;
  TEST_ASSERT_EQUAL(LearnerDecodeError::BadFlags,
                    LearnerV3Codec::decode(bad, size, target).error);
  bad[3] = 0;
  bad[size - 1] = 0;  // last cell rated high byte: still valid
  bad[LearnerV3Codec::kHeaderSize + 3] = 0xFF;  // first positive mass huge
  TEST_ASSERT_EQUAL(LearnerDecodeError::BadValue,
                    LearnerV3Codec::decode(bad, size, target).error);
  TEST_ASSERT_EQUAL_UINT32(fresh, target.fingerprint());
}

void shape_changes_keep_overlapping_cells() {
  PreferenceLearnerV3 two(productShape(2));
  uint32_t id = 1;
  for (uint32_t i = 0; i < 5; ++i) two.feedback(shown(0, 0, 1, id++), true);
  uint8_t buffer[408] = {};
  const size_t size = LearnerV3Codec::encode(two, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT32(8 + 10 * 2 * 5 * 2, size);

  PreferenceLearnerV3 four(productShape(4));
  const LearnerDecodeResult grown = LearnerV3Codec::decode(buffer, size, four);
  TEST_ASSERT_TRUE(grown.ok());
  TEST_ASSERT_EQUAL_UINT16(0, grown.droppedCells);
  TEST_ASSERT_EQUAL_UINT32(two.view(0, 0, 1).positiveQ16,
                           four.view(0, 0, 1).positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(0, four.view(0, 0, 3).nEffQ16);

  four.feedback(shown(1, 0, 3, id++), true);
  const size_t bigSize = LearnerV3Codec::encode(four, buffer, sizeof(buffer));
  PreferenceLearnerV3 shrunk(productShape(2));
  const LearnerDecodeResult result =
      LearnerV3Codec::decode(buffer, bigSize, shrunk);
  TEST_ASSERT_TRUE(result.ok());
  TEST_ASSERT_EQUAL_UINT16(1, result.droppedCells);
}

void migration_keeps_small_counts_exact() {
  LifeEngine v1;
  v1.positive[0][1] = 3;
  v1.negative[0][1] = 1;
  v1.negative[1][0] = 2;
  PreferenceLearnerV3 learner(productShape());
  TEST_ASSERT_TRUE(LearnerV3Codec::migrateFromV1(v1, learner));
  TEST_ASSERT_EQUAL_UINT32(3U << 16, learner.view(0, 0, 1).positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(1U << 16, learner.view(0, 0, 1).negativeQ16);
  TEST_ASSERT_EQUAL_UINT16(4, learner.view(0, 0, 1).rated);
  TEST_ASSERT_EQUAL_UINT32(2U << 16, learner.view(1, 0, 0).negativeQ16);
  for (uint8_t i = 0; i < 2; ++i)
    TEST_ASSERT_EQUAL_UINT8(v1.choose(i), learner.greedy(i, 0));
}

void migration_caps_long_history_and_stays_adaptable() {
  LifeEngine v1;
  v1.positive[0][1] = 600;
  v1.negative[0][1] = 200;
  v1.positive[0][0] = 10;
  v1.negative[0][0] = 90;
  PreferenceLearnerV3 learner(productShape(2));
  TEST_ASSERT_TRUE(LearnerV3Codec::migrateFromV1(v1, learner));
  const uint32_t cap = LearnerV3Codec::effectiveMemoryQ16(58982);
  const V3CellView happy = learner.view(0, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(cap, happy.positiveQ16 + happy.negativeQ16);
  // Ratio 600:200 kept within one Q16 step.
  TEST_ASSERT_UINT32_WITHIN(2, (cap * 3U) / 4U, happy.positiveQ16);
  TEST_ASSERT_EQUAL_UINT16(800, happy.rated);
  TEST_ASSERT_EQUAL_UINT8(v1.choose(0), learner.greedy(0, 0));

  // The owner now prefers action 0: a handful of ratings must flip it.
  uint32_t id = 1, ratings = 0;
  while (learner.greedy(0, 0) == 1 && ratings < 60) {
    learner.feedback(shown(0, 0, 1, id++), false);
    learner.feedback(shown(0, 0, 0, id++), true);
    ratings += 2;
  }
  TEST_ASSERT_EQUAL_UINT8(0, learner.greedy(0, 0));
  TEST_ASSERT_TRUE(ratings <= 20);
}

void explanation_reports_evidence_and_reason() {
  PreferenceLearnerV3 learner(productShape());
  Pcg32 random(1, 3);
  const V3Decision first = learner.choose(0, 3, 1, 0x0F, random);
  const V3Explanation none = LearnerV3Explainer::explain(learner, first);
  TEST_ASSERT_TRUE(none.valid);
  TEST_ASSERT_EQUAL(V3Reason::NoEvidence, none.reason);

  uint32_t id = 10;
  for (uint32_t i = 0; i < 15; ++i)
    learner.feedback(shown(0, 3, 2, id++), i < 12);
  V3Decision d = shown(0, 3, 2, 99);
  d.policyVersion = learner.policyVersion();
  const V3Explanation e = LearnerV3Explainer::explain(learner, d);
  TEST_ASSERT_EQUAL(V3Reason::HighestEstimate, e.reason);
  TEST_ASSERT_EQUAL_UINT8(2, e.greedyAction);
  TEST_ASSERT_EQUAL_UINT16(15, e.cells[2].rated);
  TEST_ASSERT_TRUE(e.cells[2].fine);
  TEST_ASSERT_TRUE(e.marginQ16 > 0);
  TEST_ASSERT_EQUAL_UINT8(4, e.actionCount);

  d.explore = true;
  d.action = 1;
  TEST_ASSERT_EQUAL(V3Reason::Exploration,
                    LearnerV3Explainer::explain(learner, d).reason);
  d.id = 0;
  TEST_ASSERT_FALSE(LearnerV3Explainer::explain(learner, d).valid);
}

void product_vocabulary_maps_and_migrates() {
  ActionId action = ActionId::Sleep;
  TEST_ASSERT_TRUE(learnActionId(2, action));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ActionId::Curious),
                          static_cast<uint8_t>(action));
  TEST_ASSERT_FALSE(learnActionId(3, action));
  uint8_t index = 9;
  TEST_ASSERT_TRUE(learnIndexFor(ActionId::Happy, index));
  TEST_ASSERT_EQUAL_UINT8(1, index);
  TEST_ASSERT_FALSE(learnIndexFor(ActionId::Sleep, index));

  const LearnerV3Config product = productLearnerConfig();
  TEST_ASSERT_EQUAL_UINT32(458, LearnerV3Codec::encodedSize(product));

  // v1 counts keep their meaning: quiet/play × idle/happy; greeting empty.
  LifeEngine v1;
  v1.positive[1][1] = 4;
  v1.negative[0][0] = 2;
  PreferenceLearnerV3 learner(product);
  TEST_ASSERT_TRUE(LearnerV3Codec::migrateFromV1(v1, learner));
  TEST_ASSERT_EQUAL_UINT32(4U << 16,
                           learner.view(static_cast<uint8_t>(
                                            LearnInteraction::PlayRequest),
                                        0, 1).positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(2U << 16, learner.view(0, 0, 0).negativeQ16);
  for (uint8_t a = 0; a < kLearnActionCount; ++a)
    TEST_ASSERT_EQUAL_UINT32(
        0, learner.view(static_cast<uint8_t>(LearnInteraction::ButtonGreeting),
                        0, a).nEffQ16);
  uint8_t buffer[458] = {};
  TEST_ASSERT_EQUAL_UINT32(458, LearnerV3Codec::encode(learner, buffer,
                                                       sizeof(buffer)));
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(encoded_size_matches_shape);
  RUN_TEST(round_trip_is_bit_exact);
  RUN_TEST(decode_rejects_corrupt_payloads_without_partial_state);
  RUN_TEST(shape_changes_keep_overlapping_cells);
  RUN_TEST(migration_keeps_small_counts_exact);
  RUN_TEST(migration_caps_long_history_and_stays_adaptable);
  RUN_TEST(explanation_reports_evidence_and_reason);
  RUN_TEST(product_vocabulary_maps_and_migrates);
  return UNITY_END();
}
