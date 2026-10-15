#include <unity.h>

#include <xuna/life/LifeCore.h>

using namespace xuna;

namespace {

const uint8_t kIdle = static_cast<uint8_t>(ActionId::Idle);
const uint8_t kHappy = static_cast<uint8_t>(ActionId::Happy);
const uint8_t kCurious = static_cast<uint8_t>(ActionId::Curious);

bool isLearnAction(uint8_t action) {
  return action == kIdle || action == kHappy || action == kCurious;
}

}  // namespace

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

void core_uses_the_product_learner_shape() {
  LifeCore core(42, 54);
  const LearnerV3Config& config = core.learner().config();
  TEST_ASSERT_EQUAL_UINT8(3, config.interactions);
  TEST_ASSERT_EQUAL_UINT8(5, config.dayParts);
  TEST_ASSERT_EQUAL_UINT8(3, config.actions);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ExplorePolicy::Ucb),
                          static_cast<uint8_t>(config.policy));
}

void core_choose_shows_a_learned_action_and_keeps_it_pending() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 1, 0, false, 0, 0};
  const LifeCoreStepOutput output = core.step({10, &choose, 1});
  TEST_ASSERT_TRUE(output.decisionShown);
  TEST_ASSERT_TRUE(output.actionAccepted);
  TEST_ASSERT_TRUE(isLearnAction(output.selectedAction));
  TEST_ASSERT_TRUE(output.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(7, output.decisionId);
  TEST_ASSERT_EQUAL_UINT8(1, output.decisionContext);
  TEST_ASSERT_EQUAL_UINT32(0, core.snapshot().counters.feedbackCount);
}

// D5: a GPIO0 press is a learnable context. XUNA chooses how to answer and
// the decision gets a device-side ID the app can rate.
void greeting_creates_a_rateable_device_decision() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  const LifeCoreStepOutput shown = core.step({10, &greeting, 1});
  TEST_ASSERT_TRUE(shown.decisionShown);
  TEST_ASSERT_TRUE(isLearnAction(shown.selectedAction));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LearnInteraction::ButtonGreeting),
                          shown.decisionContext);
  TEST_ASSERT_TRUE(shown.decisionId > LifeCore::kLocalDecisionIdBase);

  uint8_t index = 0;
  TEST_ASSERT_TRUE(learnIndexFor(static_cast<ActionId>(shown.selectedAction), index));
  const LifeCoreEvent rate{LifeCoreEventKind::Feedback, shown.decisionId, 0, 0,
                           true, 1, 0};
  const LifeCoreStepOutput applied = core.step({20, &rate, 1});
  TEST_ASSERT_TRUE(applied.learningFeedbackApplied);
  TEST_ASSERT_EQUAL_UINT16(1, core.learner().view(2, 0, index).rated);
  TEST_ASSERT_EQUAL_UINT8(2, core.snapshot().episodes.items[0].context);
  TEST_ASSERT_EQUAL_UINT8(index, core.snapshot().episodes.items[0].action);
}

// App command IDs must stay below the device range, or they could collide
// with greeting decisions in the learner's duplicate window.
void choose_rejects_ids_in_the_device_range() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent bad{LifeCoreEventKind::Choose,
                          LifeCore::kLocalDecisionIdBase + 1, 0, 0, false, 0, 0};
  TEST_ASSERT_FALSE(core.step({10, &bad, 1}).decisionShown);
  const LifeCoreEvent context{LifeCoreEventKind::Choose, 5, 2, 0, false, 0, 0};
  TEST_ASSERT_FALSE(core.step({20, &context, 1}).decisionShown);
}

void core_applies_feedback_once_and_requests_persist() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0, 0};
  const uint8_t shownAction = core.step({10, &choose, 1}).selectedAction;
  uint8_t index = 0;
  TEST_ASSERT_TRUE(learnIndexFor(static_cast<ActionId>(shownAction), index));
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1,
                               0};
  const LifeCoreStepOutput applied = core.step({20, &feedback, 1});
  TEST_ASSERT_TRUE(applied.learningFeedbackApplied);
  TEST_ASSERT_TRUE(applied.persistRequested);
  TEST_ASSERT_FALSE(applied.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(1, core.snapshot().counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(1, core.snapshot().episodes.count);
  // The owner rated the action XUNA showed; only that cell learns.
  TEST_ASSERT_EQUAL_UINT16(1, core.learner().view(0, 0, index).rated);

  const LifeCoreStepOutput duplicate = core.step({21, &feedback, 1});
  TEST_ASSERT_FALSE(duplicate.learningFeedbackApplied);
  TEST_ASSERT_FALSE(duplicate.persistRequested);
  TEST_ASSERT_EQUAL_UINT32(1, core.snapshot().counters.feedbackCount);
}

void core_rejects_feedback_without_matching_decision() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, 9, 0, 0, true, 1,
                               0};
  const LifeCoreStepOutput output = core.step({20, &feedback, 1});
  TEST_ASSERT_FALSE(output.learningFeedbackApplied);
  TEST_ASSERT_FALSE(output.persistRequested);
  TEST_ASSERT_EQUAL_UINT32(0, core.snapshot().counters.feedbackCount);
}

// MVP rule (plan §9.5): direct input invalidates the pending reaction.
void direct_request_invalidates_pending_decision() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 1, 0, false, 0, 0};
  TEST_ASSERT_TRUE(core.step({10, &choose, 1}).hasDecision);
  const LifeCoreEvent idle{LifeCoreEventKind::RequestAction, 0, 0, kIdle, false,
                           0, 0};
  TEST_ASSERT_FALSE(core.step({5000, &idle, 1}).hasDecision);
  const LifeCoreEvent late{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1, 0};
  TEST_ASSERT_FALSE(core.step({5010, &late, 1}).learningFeedbackApplied);
}

void core_maps_valid_action_to_activity_and_visual() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent action{LifeCoreEventKind::RequestAction, 0, 0, kHappy,
                             false, 0, 0};
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
  const LifeCoreEvent action{LifeCoreEventKind::RequestAction, 0, 0, kHappy,
                             false, 0, 0};
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
  const LifeCoreEvent happy{LifeCoreEventKind::RequestAction, 0, 0, kHappy,
                            false, 0, 0};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(VisualIntent::Happy),
                          static_cast<uint8_t>(core.step({10, &happy, 1}).visual));
  const LifeCoreStepOutput later =
      core.step({10 + ActivityFsm::kReactingMaxDwellMs, 0, 0});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Idle),
                          static_cast<uint8_t>(later.activity));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(VisualIntent::Idle),
                          static_cast<uint8_t>(later.visual));
}

// The learner only considers actions the FSM would accept. After 48 minutes
// of decay XUNA rests and refuses Idle (kRestExit guard), so a greeting is
// answered with happy or curious, never with a refused idle.
void choices_respect_the_allowed_mask() {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    LifeCore core(seed, 54);
    TEST_ASSERT_TRUE(core.begin(0, true));
    const LifeCoreStepOutput resting = core.step({48U * 60000U, 0, 0});
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Resting),
                            static_cast<uint8_t>(resting.activity));
    const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
    const LifeCoreStepOutput output = core.step({48U * 60000U + 10U, &greeting, 1});
    TEST_ASSERT_TRUE(output.decisionShown);
    TEST_ASSERT_TRUE(output.selectedAction == kHappy ||
                     output.selectedAction == kCurious);
  }
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
  const LifeCoreEvent happy{LifeCoreEventKind::RequestAction, 0, 0, kHappy,
                            false, 0, 0};
  output = core.step({20, &happy, 1});
  TEST_ASSERT_TRUE(output.actionAccepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::Reacting),
                          static_cast<uint8_t>(output.activity));
}

// Approved life-feel values (2026-10-15): an accepted happy reaction spends
// 10% of its registry cost and raises stimulation.
void life_feel_action_spends_energy_and_stimulates() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreStepOutput before = core.step({10, 0, 0});
  const LifeCoreEvent happy{LifeCoreEventKind::RequestAction, 0, 0, kHappy,
                            false, 0, 0};
  const LifeCoreStepOutput after = core.step({10, &happy, 1});
  TEST_ASSERT_TRUE(after.actionAccepted);
  TEST_ASSERT_EQUAL_UINT16(before.needs.energy - 409, after.needs.energy);
  TEST_ASSERT_EQUAL_UINT16(4096, after.needs.stimulation);
}

// The owner keeps rejecting happy at the greeting and approving curious:
// XUNA's greeting preference moves to curious (product shape, E10 learner).
void greeting_preference_follows_owner_ratings() {
  LifeCore core(7, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  MonoMs now = 10;
  for (int i = 0; i < 30; ++i) {
    const LifeCoreStepOutput shown = core.step({now, &greeting, 1});
    now += 3000;
    if (!shown.decisionShown) continue;
    const LifeCoreEvent rate{LifeCoreEventKind::Feedback, shown.decisionId, 0,
                             0, shown.selectedAction == kCurious, 1, 0};
    core.step({now, &rate, 1});
    now += 3000;
  }
  TEST_ASSERT_EQUAL_UINT8(2, core.preferred(LearnInteraction::ButtonGreeting));
  // Quiet and play contexts are untouched by greeting ratings.
  TEST_ASSERT_EQUAL_UINT16(0, core.learner().view(0, 0, 2).rated);
}

void forget_clears_learning_but_keeps_life_counters() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  core.snapshot().counters.activeSeconds = 900;
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0, 0};
  core.step({10, &choose, 1});
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1, 0};
  TEST_ASSERT_TRUE(core.step({20, &feedback, 1}).learningFeedbackApplied);
  TEST_ASSERT_TRUE(core.forget());
  TEST_ASSERT_EQUAL_UINT32(0, core.snapshot().counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(0, core.snapshot().episodes.count);
  TEST_ASSERT_EQUAL_UINT32(900, core.snapshot().counters.activeSeconds);
  TEST_ASSERT_EQUAL_UINT32(PreferenceLearnerV3(productLearnerConfig()).fingerprint(),
                           core.learner().fingerprint());
}

// ---- S10 host part: trusted time and day parts ---------------------------

namespace {

const int16_t kVietnam = 420;  // UTC+7
// 2020-01-01 01:00 UTC = 08:00 local (morning); 12:00 UTC = 19:00 (evening).
const int64_t kMorningUtc = ClockModel::kEpochUtcMs + 3600000LL;
const int64_t kEveningUtc = ClockModel::kEpochUtcMs + 12LL * 3600000LL;

LifeCoreStepOutput greetAndRate(LifeCore& core, MonoMs& now,
                                uint8_t approvedAction) {
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  const LifeCoreStepOutput shown = core.step({now, &greeting, 1});
  now += 3000;
  if (shown.decisionShown) {
    const LifeCoreEvent rate{LifeCoreEventKind::Feedback, shown.decisionId, 0,
                             0, shown.selectedAction == approvedAction, 1, 0};
    core.step({now, &rate, 1});
    now += 3000;
  }
  return shown;
}

}  // namespace

void decisions_use_the_coarse_cell_until_time_is_trusted() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  MonoMs now = 10;
  const LifeCoreStepOutput shown = greetAndRate(core, now, kHappy);
  TEST_ASSERT_TRUE(shown.decisionShown);
  uint8_t index = 0;
  learnIndexFor(static_cast<ActionId>(shown.selectedAction), index);
  TEST_ASSERT_EQUAL_UINT16(1, core.learner().view(2, 0, index).rated);
  for (uint8_t d = 1; d < 5; ++d)
    TEST_ASSERT_FALSE(core.learner().view(2, d, index).fine);
  TEST_ASSERT_EQUAL_UINT32(0, core.snapshot().clock.lastTrustedUtcMinute);
}

void synced_decisions_learn_in_the_day_part_cell() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::Accepted),
                          static_cast<uint8_t>(core.sync(kMorningUtc, kVietnam, TimeSource::Phone, 5).status));
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  const LifeCoreStepOutput shown = core.step({10, &greeting, 1});
  TEST_ASSERT_TRUE(shown.decisionShown);
  TEST_ASSERT_EQUAL_UINT8(1, shown.decisionDayPart);  // morning
  uint8_t index = 0;
  learnIndexFor(static_cast<ActionId>(shown.selectedAction), index);
  const LifeCoreEvent rate{LifeCoreEventKind::Feedback, shown.decisionId, 0, 0,
                           true, 1, 0};
  TEST_ASSERT_TRUE(core.step({20, &rate, 1}).learningFeedbackApplied);
  // Both the morning cell and the coarse cell learn (plan §9.3).
  const V3CellView morning = core.learner().view(2, 1, index);
  TEST_ASSERT_TRUE(morning.fine);
  TEST_ASSERT_EQUAL_UINT16(1, morning.rated);
  TEST_ASSERT_EQUAL_UINT16(1, core.learner().view(2, 0, index).rated);
  // The evening cell has no evidence yet, so it falls back to the coarse one.
  TEST_ASSERT_FALSE(core.learner().view(2, 3, index).fine);
  // The watermark is in the RAM snapshot for the next commit.
  TEST_ASSERT_EQUAL_UINT32(core.clock().lastTrustedUtcMinute(),
                           core.snapshot().clock.lastTrustedUtcMinute);
  TEST_ASSERT_EQUAL_INT16(kVietnam, core.snapshot().clock.tzOffsetMin);
}

// The owner likes a happy greeting in the morning and a curious one in the
// evening. With trusted time XUNA keeps both: one preference per day part.
void morning_and_evening_greeting_preferences_diverge() {
  LifeCore core(11, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  MonoMs now = 10;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::Accepted),
                          static_cast<uint8_t>(core.sync(kMorningUtc, kVietnam, TimeSource::Phone, now).status));
  for (int i = 0; i < 40; ++i) greetAndRate(core, now, kHappy);
  // Eleven hours later (no resync needed): 19:00 local, evening.
  now = 10 + 11U * 3600000U;
  core.step({now, 0, 0});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DayPart::Evening),
                          static_cast<uint8_t>(core.clock().dayPart(core.mono64())));
  for (int i = 0; i < 40; ++i) greetAndRate(core, now, kCurious);
  TEST_ASSERT_EQUAL_UINT8(1, core.preferred(LearnInteraction::ButtonGreeting, 1));
  TEST_ASSERT_EQUAL_UINT8(2, core.preferred(LearnInteraction::ButtonGreeting, 3));
  // A later evening greeting answers with curious, not the morning habit.
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  const LifeCoreStepOutput evening = core.step({now + 60000U, &greeting, 1});
  TEST_ASSERT_EQUAL_UINT8(3, evening.decisionDayPart);
}

void rollback_sync_is_rejected_and_keeps_the_clock() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  core.sync(kEveningUtc, kVietnam, TimeSource::Phone, 10);
  const uint32_t watermark = core.snapshot().clock.lastTrustedUtcMinute;
  const SyncResult back = core.sync(kMorningUtc, kVietnam, TimeSource::Phone, 20);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::RejectedRollback),
                          static_cast<uint8_t>(back.status));
  TEST_ASSERT_EQUAL_UINT32(watermark, core.snapshot().clock.lastTrustedUtcMinute);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DayPart::Evening),
                          static_cast<uint8_t>(core.clock().dayPart(core.mono64())));
}

// millis() wraps after ~49.7 days; trusted time and later syncs must survive.
void clock_survives_millis_wraparound() {
  LifeCore core(42, 54);
  const MonoMs start = 0xFFFFF000U;
  TEST_ASSERT_TRUE(core.begin(start, true));
  core.sync(kMorningUtc, kVietnam, TimeSource::Phone, start + 10U);
  const MonoMs after = start + 0x2000U;  // wrapped past zero
  TEST_ASSERT_TRUE(after < start);
  const LifeCoreStepOutput out = core.step({after, 0, 0});
  (void)out;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ClockQuality::Synced),
                          static_cast<uint8_t>(core.clock().quality(core.mono64())));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::Accepted),
                          static_cast<uint8_t>(core.sync(kMorningUtc + 0x2000LL, kVietnam, TimeSource::Phone, after + 5U).status));
}

// ---- D3: 120 s feedback window (owner-approved 2026-10-15) ---------------

void rating_at_the_window_edge_is_accepted() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0, 0};
  core.step({1000, &choose, 1});
  const LifeCoreEvent rate{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1, 0};
  const LifeCoreStepOutput out = core.step({1000 + 120000, &rate, 1});
  TEST_ASSERT_TRUE(out.learningFeedbackApplied);
  TEST_ASSERT_FALSE(out.feedbackExpired);
}

void late_rating_is_rejected_as_expired_and_teaches_nothing() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0, 0};
  core.step({1000, &choose, 1});
  const uint32_t before = core.learner().fingerprint();
  const LifeCoreEvent rate{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1, 0};
  const LifeCoreStepOutput late = core.step({1000 + 120001, &rate, 1});
  TEST_ASSERT_TRUE(late.feedbackExpired);
  TEST_ASSERT_FALSE(late.learningFeedbackApplied);
  TEST_ASSERT_FALSE(late.persistRequested);
  TEST_ASSERT_FALSE(late.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(before, core.learner().fingerprint());
  TEST_ASSERT_EQUAL_UINT32(0, core.snapshot().counters.feedbackCount);
  // A repeated late rating still reports expired, not unknown.
  TEST_ASSERT_TRUE(core.step({1000 + 125000, &rate, 1}).feedbackExpired);
}

void unrated_decision_times_out_once() {
  LifeCore core(42, 54);
  TEST_ASSERT_TRUE(core.begin(0, true));
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  const uint32_t id = core.step({1000, &greeting, 1}).decisionId;
  TEST_ASSERT_TRUE(core.step({1000 + 120000, 0, 0}).hasDecision);
  const LifeCoreStepOutput timedOut = core.step({1000 + 120001, 0, 0});
  TEST_ASSERT_TRUE(timedOut.decisionExpired);
  TEST_ASSERT_FALSE(timedOut.hasDecision);
  TEST_ASSERT_FALSE(core.step({1000 + 121000, 0, 0}).decisionExpired);
  const LifeCoreEvent rate{LifeCoreEventKind::Feedback, id, 0, 0, true, 1, 0};
  TEST_ASSERT_TRUE(core.step({1000 + 122000, &rate, 1}).feedbackExpired);
}

void feedback_window_survives_millis_wraparound() {
  LifeCore core(42, 54);
  const MonoMs start = 0xFFFF0000U;
  TEST_ASSERT_TRUE(core.begin(start, true));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 7, 0, 0, false, 0, 0};
  core.step({start + 10U, &choose, 1});
  const LifeCoreEvent rate{LifeCoreEventKind::Feedback, 7, 0, 0, true, 1, 0};
  const MonoMs wrapped = start + 10U + 90000U;  // past zero, 90 s later
  TEST_ASSERT_TRUE(wrapped < start);
  TEST_ASSERT_TRUE(core.step({wrapped, &rate, 1}).learningFeedbackApplied);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(life_feel_action_spends_energy_and_stimulates);
  RUN_TEST(core_starts_and_ticks_local_state);
  RUN_TEST(core_uses_the_product_learner_shape);
  RUN_TEST(core_choose_shows_a_learned_action_and_keeps_it_pending);
  RUN_TEST(greeting_creates_a_rateable_device_decision);
  RUN_TEST(choose_rejects_ids_in_the_device_range);
  RUN_TEST(core_applies_feedback_once_and_requests_persist);
  RUN_TEST(core_rejects_feedback_without_matching_decision);
  RUN_TEST(direct_request_invalidates_pending_decision);
  RUN_TEST(core_maps_valid_action_to_activity_and_visual);
  RUN_TEST(core_begin_resets_activity_and_action_cooldowns);
  RUN_TEST(core_action_visual_ends_with_its_activity);
  RUN_TEST(choices_respect_the_allowed_mask);
  RUN_TEST(core_wakes_from_sleep_for_actions);
  RUN_TEST(greeting_preference_follows_owner_ratings);
  RUN_TEST(forget_clears_learning_but_keeps_life_counters);
  RUN_TEST(decisions_use_the_coarse_cell_until_time_is_trusted);
  RUN_TEST(synced_decisions_learn_in_the_day_part_cell);
  RUN_TEST(morning_and_evening_greeting_preferences_diverge);
  RUN_TEST(rollback_sync_is_rejected_and_keeps_the_clock);
  RUN_TEST(clock_survives_millis_wraparound);
  RUN_TEST(rating_at_the_window_edge_is_accepted);
  RUN_TEST(late_rating_is_rejected_as_expired_and_teaches_nothing);
  RUN_TEST(unrated_decision_times_out_once);
  RUN_TEST(feedback_window_survives_millis_wraparound);
  return UNITY_END();
}
