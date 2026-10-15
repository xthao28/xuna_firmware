#include <unity.h>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/learning/LearningBridge.h>
#include <xuna/learning/LearningBridgeV3.h>
#include <xuna/learning/PreferenceLearnerV2.h>

using namespace xuna;

void bridge_uses_existing_v1_state_without_changing_contract() {
  LifeEngine baseline;
  baseline.feedback(0, 1, true, 1);
  PreferenceLearnerV2 candidate;
  Pcg32 random(42, 54);
  LearningBridge bridge(baseline, candidate, random);

  const LearningBridgeDecision decision = bridge.choose(0, 9, 100);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.metrics().decisions);
  TEST_ASSERT_EQUAL_UINT32(1, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, candidate.metrics().feedback);
  TEST_ASSERT_TRUE(bridge.feedback(decision, true, 1, 120));
  TEST_ASSERT_EQUAL_UINT32(2, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, candidate.metrics().feedback);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.metrics().feedbackAccepted);

  TEST_ASSERT_FALSE(bridge.feedback(decision, true, 1, 121));
  TEST_ASSERT_EQUAL_UINT32(2, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.metrics().feedbackRejected);
}

void bridge_rejects_invalid_decisions_before_training_v1() {
  LifeEngine baseline;
  PreferenceLearnerV2 candidate;
  Pcg32 random(7, 11);
  LearningBridge bridge(baseline, candidate, random);
  LearningBridgeDecision invalid;
  invalid.id = 22;
  invalid.candidate.id = 0;

  TEST_ASSERT_FALSE(bridge.feedback(invalid, true, 1, 30));
  TEST_ASSERT_EQUAL_UINT32(0, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, candidate.metrics().feedback);
}

void bridge_rejects_out_of_contract_feedback_source() {
  LifeEngine baseline;
  PreferenceLearnerV2 candidate;
  Pcg32 random(7, 11);
  LearningBridge bridge(baseline, candidate, random);
  const LearningBridgeDecision decision = bridge.choose(0, 1, 10);

  TEST_ASSERT_FALSE(bridge.feedback(decision, true, 3, 20));
  TEST_ASSERT_EQUAL_UINT32(0, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, candidate.metrics().feedback);
}

void bridge_v3_credits_shown_action_and_trains_v1_once() {
  LifeEngine baseline;
  PreferenceLearnerV3 candidate;
  Pcg32 random(3, 3);
  LearningBridgeV3 bridge(baseline, candidate, random);
  const LearningBridgeV3Decision decision = bridge.choose(1, 0, 5, 100);
  TEST_ASSERT_EQUAL_UINT32(5, decision.candidate.id);
  TEST_ASSERT_EQUAL_UINT8(baseline.choose(1), decision.baselineAction);
  TEST_ASSERT_TRUE(bridge.feedback(decision, true, 1, 110));
  TEST_ASSERT_EQUAL_UINT32(1, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT16(
      1, candidate.view(1, 0, decision.baselineAction).rated);
  // Duplicate: rejected by the candidate, v1 not trained again.
  TEST_ASSERT_FALSE(bridge.feedback(decision, true, 1, 120));
  TEST_ASSERT_EQUAL_UINT32(1, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.metrics().feedbackAccepted);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.metrics().feedbackRejected);
  TEST_ASSERT_EQUAL_UINT32(1, bridge.metrics().decisions);
}

void bridge_v3_rejects_bad_source_and_context() {
  LifeEngine baseline;
  PreferenceLearnerV3 candidate;
  Pcg32 random(3, 3);
  LearningBridgeV3 bridge(baseline, candidate, random);
  LearningBridgeV3Decision decision = bridge.choose(0, 0, 9, 10);
  const uint32_t fresh = candidate.fingerprint();
  TEST_ASSERT_FALSE(bridge.feedback(decision, true, 0, 20));
  TEST_ASSERT_FALSE(bridge.feedback(decision, true, 3, 20));
  decision.context = 2;
  TEST_ASSERT_FALSE(bridge.feedback(decision, true, 1, 20));
  TEST_ASSERT_EQUAL_UINT32(0, baseline.eventCount);
  TEST_ASSERT_EQUAL_UINT32(fresh, candidate.fingerprint());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(bridge_v3_credits_shown_action_and_trains_v1_once);
  RUN_TEST(bridge_v3_rejects_bad_source_and_context);
  RUN_TEST(bridge_uses_existing_v1_state_without_changing_contract);
  RUN_TEST(bridge_rejects_invalid_decisions_before_training_v1);
  RUN_TEST(bridge_rejects_out_of_contract_feedback_source);
  return UNITY_END();
}
