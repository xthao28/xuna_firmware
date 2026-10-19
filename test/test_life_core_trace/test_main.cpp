#include <cstring>

#include <unity.h>

#include <xuna/life/LifeCoreTrace.h>

using namespace xuna;

LearningTraceDslCommand command(uint32_t at, LearningTraceVerb verb,
                                uint32_t value0 = 0, uint32_t value1 = 0,
                                bool flag = false) {
  LearningTraceDslCommand result;
  result.atMs = at;
  result.verb = verb;
  result.value0 = value0;
  result.value1 = value1;
  result.flag = flag;
  return result;
}

void core_trace_replays_choice_feedback_and_button() {
  LearningTraceDslCommand commands[3] = {};
  commands[0] = command(10, LearningTraceVerb::Choose, 1, 0);
  commands[1] = command(20, LearningTraceVerb::Feedback, 1, 1, true);
  commands[2] = command(30, LearningTraceVerb::Button);
  commands[2].value0 = 'A';
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_TRUE(runner.run(commands, 3, report, 42, 54));
  TEST_ASSERT_EQUAL_UINT32(3, report.processed);
  TEST_ASSERT_EQUAL_UINT32(1, report.choices);
  TEST_ASSERT_EQUAL_UINT32(1, report.feedbackApplied);
  TEST_ASSERT_EQUAL_UINT32(1, report.persistRequests);
  TEST_ASSERT_EQUAL_UINT32(0, report.persistFailures);
  TEST_ASSERT_EQUAL_UINT32(1, report.finalEventCount);
  TEST_ASSERT_EQUAL_UINT32(1, report.finalGeneration);
  TEST_ASSERT_NOT_EQUAL_UINT32(0, report.fingerprint);
}

void core_trace_replays_powercut_and_reboot_without_losing_commit() {
  LearningTraceDslCommand commands[6] = {};
  commands[0] = command(10, LearningTraceVerb::Choose, 1, 0);
  commands[1] = command(20, LearningTraceVerb::Feedback, 1, 1, true);
  commands[2] = command(30, LearningTraceVerb::Powercut);
  commands[3] = command(40, LearningTraceVerb::Choose, 2, 0);
  commands[4] = command(50, LearningTraceVerb::Feedback, 2, 1, true);
  commands[5] = command(60, LearningTraceVerb::Reboot);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_TRUE(runner.run(commands, 6, report, 42, 54));
  TEST_ASSERT_EQUAL_UINT32(6, report.processed);
  TEST_ASSERT_EQUAL_UINT32(2, report.persistRequests);
  TEST_ASSERT_EQUAL_UINT32(1, report.persistFailures);
  TEST_ASSERT_EQUAL_UINT32(1, report.reboots);
  TEST_ASSERT_EQUAL_UINT32(1, report.finalEventCount);
  TEST_ASSERT_EQUAL_UINT32(1, report.finalGeneration);
}

LearningTraceDslCommand expect(uint32_t at, const char* path, const char* op,
                               uint32_t value) {
  LearningTraceDslCommand result = command(at, LearningTraceVerb::Expect, value);
  std::strncpy(result.text, path, sizeof(result.text) - 1);
  std::strncpy(result.op, op, sizeof(result.op) - 1);
  return result;
}

void core_trace_day_steps_past_millis_wrap() {
  LearningTraceDslCommand commands[4] = {};
  commands[0] = command(10, LearningTraceVerb::Choose, 1, 0);
  commands[1] = command(20, LearningTraceVerb::Feedback, 1, 1, true);
  commands[2] = command(30, LearningTraceVerb::Day, 60);
  commands[3] = command(40, LearningTraceVerb::Choose, 2, 1);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_TRUE(runner.run(commands, 4, report, 42, 54));
  TEST_ASSERT_EQUAL_UINT32(4, report.processed);
  TEST_ASSERT_EQUAL_UINT32(0, report.unsupported);
  TEST_ASSERT_EQUAL_UINT32(60, report.simulatedDays);
  // 60 days > 2^32 ms: the core keeps a 64-bit clock across the wrap.
  TEST_ASSERT_TRUE(report.finalMonoMs >= 60ULL * LifeCoreTraceRunner::kDayMs);
  TEST_ASSERT_TRUE(report.finalMonoMs > 0xFFFFFFFFULL);
  TEST_ASSERT_EQUAL_UINT32(1, report.finalEventCount);
  // LifeCore alone has no lazy checkpoint (active time lives in the v1
  // adapter), so 60 quiet days write nothing beyond the feedback commit.
  TEST_ASSERT_EQUAL_UINT32(1, report.persistRequests);
  TEST_ASSERT_EQUAL_UINT32(1, report.finalGeneration);
}

void core_trace_expect_counts_failures_and_rejects_unknown_paths() {
  LearningTraceDslCommand commands[5] = {};
  commands[0] = command(10, LearningTraceVerb::Choose, 1, 0);
  commands[1] = command(20, LearningTraceVerb::Feedback, 1, 1, true);
  commands[2] = expect(30, "feedback_count", "==", 1);
  commands[3] = expect(40, "generation", ">=", 5);
  commands[4] = expect(50, "reboots", "==", 0);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_TRUE(runner.run(commands, 5, report, 42, 54));
  TEST_ASSERT_EQUAL_UINT32(3, report.expectChecked);
  TEST_ASSERT_EQUAL_UINT32(1, report.expectFailed);
  TEST_ASSERT_EQUAL_UINT32(4, report.firstFailedCommand);
  TEST_ASSERT_EQUAL_UINT32(1, report.firstFailedActual);

  commands[2] = expect(30, "feedbackCount", "==", 1);
  TEST_ASSERT_FALSE(runner.run(commands, 3, report, 42, 54));
}

void core_trace_reboot_restarts_the_core_clock() {
  LearningTraceDslCommand commands[3] = {};
  commands[0] = command(10, LearningTraceVerb::Day, 2);
  commands[1] = command(20, LearningTraceVerb::Reboot);
  commands[2] = command(5020, LearningTraceVerb::Choose, 1, 0);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_TRUE(runner.run(commands, 3, report, 42, 54));
  // millis() starts at 0 after a reboot, as on the board.
  TEST_ASSERT_EQUAL_UINT64(5000, report.finalMonoMs);
}

void core_trace_rejects_zero_or_huge_day() {
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  const LearningTraceDslCommand zero = command(10, LearningTraceVerb::Day, 0);
  TEST_ASSERT_FALSE(runner.run(&zero, 1, report));
  const LearningTraceDslCommand huge =
      command(10, LearningTraceVerb::Day, LifeCoreTraceRunner::kMaxDays + 1);
  TEST_ASSERT_FALSE(runner.run(&huge, 1, report));
}

void core_trace_is_deterministic_and_explains_the_last_decision() {
  LearningTraceDslCommand commands[4] = {};
  commands[0] = command(10, LearningTraceVerb::Choose, 1, 1);
  commands[1] = command(20, LearningTraceVerb::Feedback, 1, 1, true);
  commands[2] = command(30, LearningTraceVerb::Day, 2);
  commands[3] = command(40, LearningTraceVerb::Choose, 2, 1);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport first;
  LifeCoreTraceReport second;
  TEST_ASSERT_TRUE(runner.run(commands, 4, first, 42, 54));
  TEST_ASSERT_TRUE(runner.run(commands, 4, second, 42, 54));
  TEST_ASSERT_EQUAL_UINT32(first.fingerprint, second.fingerprint);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first.snapshotSha256, second.snapshotSha256,
                                sizeof(first.snapshotSha256));
  TEST_ASSERT_TRUE(first.explanation.valid);
  TEST_ASSERT_EQUAL_UINT8(1, first.explanation.interaction);  // play
  TEST_ASSERT_TRUE(first.explanation.actionCount > 0);
}

void core_trace_rejects_non_monotonic_time() {
  LearningTraceDslCommand commands[2] = {};
  commands[0] = command(20, LearningTraceVerb::Choose, 1, 0);
  commands[1] = command(10, LearningTraceVerb::Feedback, 1, 1, true);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_FALSE(runner.run(commands, 2, report));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(core_trace_replays_choice_feedback_and_button);
  RUN_TEST(core_trace_replays_powercut_and_reboot_without_losing_commit);
  RUN_TEST(core_trace_day_steps_past_millis_wrap);
  RUN_TEST(core_trace_expect_counts_failures_and_rejects_unknown_paths);
  RUN_TEST(core_trace_reboot_restarts_the_core_clock);
  RUN_TEST(core_trace_rejects_zero_or_huge_day);
  RUN_TEST(core_trace_is_deterministic_and_explains_the_last_decision);
  RUN_TEST(core_trace_rejects_non_monotonic_time);
  return UNITY_END();
}