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

void core_trace_counts_future_verbs_without_interpreting_them() {
  const LearningTraceDslCommand commandValue =
      command(10, LearningTraceVerb::Day);
  LifeCoreTraceRunner runner;
  LifeCoreTraceReport report;
  TEST_ASSERT_TRUE(runner.run(&commandValue, 1, report));
  TEST_ASSERT_EQUAL_UINT32(1, report.processed);
  TEST_ASSERT_EQUAL_UINT32(1, report.unsupported);
  TEST_ASSERT_EQUAL_UINT32(0, report.rejected);
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
  RUN_TEST(core_trace_counts_future_verbs_without_interpreting_them);
  RUN_TEST(core_trace_rejects_non_monotonic_time);
  return UNITY_END();
}