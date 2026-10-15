#include <unity.h>

#include <xuna/life/LifeCore.h>

using namespace xuna;

void core_starts_and_ticks_local_state() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(100, true));
  const LifeCoreStepOutput output = core.step({100, 0, 0});
  TEST_ASSERT_TRUE(output.started);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Idle),
                          static_cast<uint8_t>(output.activity));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(VisualIntent::Idle),
                          static_cast<uint8_t>(output.visual));
}

void core_runs_v1_and_v2_choice_from_existing_state() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0,
                             0};
  const LifeCoreStepOutput output = core.step({10, &choose, 1});
  TEST_ASSERT_TRUE(output.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(7, output.decisionId);
  TEST_ASSERT_EQUAL_UINT8(0, output.baselineAction);
  TEST_ASSERT_TRUE(output.candidateAction <= 1);
  TEST_ASSERT_EQUAL_UINT32(0, core.life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(0, core.shadowMetrics().feedbackAccepted);
}

void core_applies_feedback_once_and_requests_v1_persist() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0,
                             0};
  core.step({10, &choose, 1});
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1,
                               0};
  const LifeCoreStepOutput applied = core.step({20, &feedback, 1});
  TEST_ASSERT_TRUE(applied.learningFeedbackApplied);
  TEST_ASSERT_TRUE(applied.persistRequested);
  TEST_ASSERT_FALSE(applied.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(1, core.life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, core.shadowMetrics().feedbackAccepted);
  // v3 is credited on the action XUNA showed (v1's), the one the owner rated.
  TEST_ASSERT_EQUAL_UINT16(1, core.candidate().view(0, 0, 0).rated);
  TEST_ASSERT_EQUAL_UINT16(0, core.candidate().view(0, 0, 1).rated);

  const LifeCoreStepOutput duplicate = core.step({21, &feedback, 1});
  TEST_ASSERT_FALSE(duplicate.learningFeedbackApplied);
  TEST_ASSERT_FALSE(duplicate.persistRequested);
  TEST_ASSERT_EQUAL_UINT32(1, core.life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, core.shadowMetrics().feedbackAccepted);
}

void core_rejects_feedback_without_matching_decision() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, 9, 0, 0, true, 1,
                               0};
  const LifeCoreStepOutput output = core.step({20, &feedback, 1});
  TEST_ASSERT_FALSE(output.learningFeedbackApplied);
  TEST_ASSERT_FALSE(output.persistRequested);
  TEST_ASSERT_EQUAL_UINT32(0, core.life().eventCount);
}

void core_maps_valid_action_to_activity_and_visual() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent action{LifeCoreEventKind::RequestAction, 0, 0, 1, false,
                             0, 0};
  const LifeCoreStepOutput output = core.step({10, &action, 1});
  TEST_ASSERT_TRUE(output.actionAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Reacting),
                          static_cast<uint8_t>(output.activity));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(VisualIntent::Happy),
                          static_cast<uint8_t>(output.visual));
}

void core_begin_resets_activity_and_action_cooldowns() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent action{LifeCoreEventKind::RequestAction, 0, 0, 1, false,
                             0, 0};
  TEST_ASSERT_TRUE(core.step({10, &action, 1}).actionAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Reacting),
                          static_cast<uint8_t>(core.activity().state()));

  TEST_ASSERT_TRUE(core.begin(20, true));
  const LifeCoreStepOutput rebooted = core.step({20, 0, 0});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Idle),
                          static_cast<uint8_t>(rebooted.activity));
  TEST_ASSERT_TRUE(core.step({21, &action, 1}).actionAccepted);
}
void core_action_visual_ends_with_its_activity() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent happy{LifeCoreEventKind::RequestAction, 0, 0, 1, false,
                            0, 0};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(VisualIntent::Happy),
                          static_cast<uint8_t>(core.step({10, &happy, 1}).visual));
  const LifeCoreStepOutput later =
      core.step({10 + ActivityFsm::kReactingMaxDwellMs, 0, 0});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Idle),
                          static_cast<uint8_t>(later.activity));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(VisualIntent::Idle),
                          static_cast<uint8_t>(later.visual));
}

void core_rejected_choice_keeps_previous_decision() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent first{LifeCoreEventKind::Choose, 7, 0, 0, false, 0, 0};
  LifeCoreStepOutput output = core.step({10, &first, 1});
  TEST_ASSERT_TRUE(output.actionAccepted);
  TEST_ASSERT_EQUAL_UINT8(0, output.selectedAction);
  TEST_ASSERT_EQUAL_UINT32(7, output.decisionId);

  // After 48 minutes of default decay energy is 16383: the tick enters
  // Resting, and the baseline Idle choice is refused by the kRestExit guard.
  const LifeCoreEvent second{LifeCoreEventKind::Choose, 8, 0, 0, false, 0, 0};
  output = core.step({48U * 60000U + 10U, &second, 1});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Resting),
                          static_cast<uint8_t>(output.activity));
  TEST_ASSERT_FALSE(output.actionAccepted);
  TEST_ASSERT_TRUE(output.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(7, output.decisionId);

  const LifeCoreEvent stale{LifeCoreEventKind::Feedback, 8, 0, 0, true, 1, 0};
  TEST_ASSERT_FALSE(core.step({48U * 60000U + 20U, &stale, 1})
                        .learningFeedbackApplied);
  const LifeCoreEvent shown{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1, 0};
  TEST_ASSERT_TRUE(core.step({48U * 60000U + 30U, &shown, 1})
                       .learningFeedbackApplied);
  TEST_ASSERT_EQUAL_UINT32(1, core.life().eventCount);
}

void core_wakes_from_sleep_for_actions() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent sleep{LifeCoreEventKind::RequestAction, 0, 0, 2, false,
                            0, 0};
  LifeCoreStepOutput output = core.step({10, &sleep, 1});
  TEST_ASSERT_TRUE(output.actionAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Sleeping),
                          static_cast<uint8_t>(output.activity));
  const LifeCoreEvent happy{LifeCoreEventKind::RequestAction, 0, 0, 1, false,
                            0, 0};
  output = core.step({20, &happy, 1});
  TEST_ASSERT_TRUE(output.actionAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Reacting),
                          static_cast<uint8_t>(output.activity));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(core_starts_and_ticks_local_state);
  RUN_TEST(core_runs_v1_and_v2_choice_from_existing_state);
  RUN_TEST(core_applies_feedback_once_and_requests_v1_persist);
  RUN_TEST(core_rejects_feedback_without_matching_decision);
  RUN_TEST(core_maps_valid_action_to_activity_and_visual);
  RUN_TEST(core_begin_resets_activity_and_action_cooldowns);
  RUN_TEST(core_action_visual_ends_with_its_activity);
  RUN_TEST(core_rejected_choice_keeps_previous_decision);
  RUN_TEST(core_wakes_from_sleep_for_actions);
  return UNITY_END();
}
