#include <unity.h>
#include <xuna/base/Random.h>
#include <xuna/learning/PrequentialEvaluator.h>
#include <xuna/learning/LearningShadow.h>
#include <xuna/learning/LearningTrace.h>
#include <xuna/learning/LearningExplanation.h>
#include <xuna/learning/LearningTraceDsl.h>
#include <xuna/learning/PreferenceLearnerV2.h>

using namespace xuna;

PreferenceLearnerV2 makeTrace(uint32_t& lastAction) {
  LearningConfig config;
  config.contextCount = 2;
  config.actionCount = 2;
  config.explorationQ16 = 16384;
  PreferenceLearnerV2 learner(config);
  Pcg32 random(42, 54);
  for (uint32_t id = 1; id <= 8; ++id) {
    const LearningDecision decision = learner.choose(0, id, id * 100, random);
    lastAction = decision.action;
    if (decision.id != id ||
        !learner.feedback(decision, id % 2 == 0, 1, id * 100 + 20)) {
      lastAction = 255;
    }
  }
  return learner;
}

void starts_uncertain_and_explores() {
  LearningConfig config;
  config.contextCount = 1;
  config.actionCount = 3;
  PreferenceLearnerV2 learner(config);
  Pcg32 random(7, 11);
  const LearningDecision decision = learner.choose(0, 1, 10, random);
  TEST_ASSERT_TRUE(decision.exploratory);
  TEST_ASSERT_EQUAL_UINT16(65535U / 3U, decision.propensityQ16);
  TEST_ASSERT_EQUAL_UINT32(1, learner.metrics().decisions);
  TEST_ASSERT_EQUAL_UINT32(1, learner.metrics().exploratoryDecisions);
}

void feedback_is_contextual_and_idempotent() {
  PreferenceLearnerV2 learner;
  Pcg32 random(1, 3);
  const LearningDecision first = learner.choose(0, 1, 100, random);
  TEST_ASSERT_TRUE(learner.feedback(first, true, 2, 150));
  TEST_ASSERT_FALSE(learner.feedback(first, true, 2, 151));
  TEST_ASSERT_EQUAL_UINT32(1, learner.metrics().feedback);
  TEST_ASSERT_EQUAL_UINT32(1, learner.metrics().duplicateFeedback);
  TEST_ASSERT_EQUAL_UINT16(1, learner.posterior(0, first.action).positive);
  TEST_ASSERT_EQUAL_UINT16(0, learner.posterior(1, first.action).positive);

  TEST_ASSERT_FALSE(learner.feedback(first, true, 0, 160));
  TEST_ASSERT_EQUAL_UINT32(1, learner.metrics().invalidFeedback);
}

void positive_feedback_changes_future_policy() {
  PreferenceLearnerV2 learner;
  Pcg32 random(99, 101);
  for (uint32_t id = 1; id <= 4; ++id) {
    LearningDecision decision;
    decision.id = id;
    decision.context = 0;
    decision.action = 0;
    decision.decidedAt = id;
    TEST_ASSERT_TRUE(learner.feedback(decision, false, 1, id + 10));
  }
  for (uint32_t id = 5; id <= 8; ++id) {
    LearningDecision decision;
    decision.id = id;
    decision.context = 0;
    decision.action = 1;
    decision.decidedAt = id;
    TEST_ASSERT_TRUE(learner.feedback(decision, true, 1, id + 10));
  }
  const LearningPosterior idle = learner.posterior(0, 0);
  const LearningPosterior happy = learner.posterior(0, 1);
  TEST_ASSERT_TRUE(happy.meanQ16 > idle.meanQ16);
  const LearningDecision future = learner.choose(0, 100, 1000, random);
  TEST_ASSERT_EQUAL_UINT8(1, future.action);
  TEST_ASSERT_TRUE(learner.metrics().feedback == 8);
}

void context_does_not_leak() {
  PreferenceLearnerV2 learner;
  Pcg32 random(12, 13);
  for (uint32_t id = 1; id <= 4; ++id) {
    const LearningDecision decision = learner.choose(0, id, id, random);
    TEST_ASSERT_TRUE(learner.feedback(decision, true, 1, id + 20));
  }
  TEST_ASSERT_EQUAL_UINT16(0, learner.posterior(1, 0).positive);
  TEST_ASSERT_EQUAL_UINT16(0, learner.posterior(1, 1).positive);
}

void evidence_is_bounded_and_replay_is_deterministic() {
  LearningConfig config;
  config.maxEvidence = 4;
  PreferenceLearnerV2 learner(config);
  Pcg32 random(5, 8);
  for (uint32_t id = 1; id <= 20; ++id) {
    LearningDecision decision = learner.choose(0, id, id, random);
    decision.action = 0;
    TEST_ASSERT_TRUE(learner.feedback(decision, true, 1, id + 1));
  }
  TEST_ASSERT_TRUE(learner.posterior(0, 0).positive <= 4);
  TEST_ASSERT_EQUAL_UINT8(8, learner.historyCount());
  TEST_ASSERT_EQUAL_UINT32(13, learner.history(0).decisionId);

  uint32_t action = 0;
  const PreferenceLearnerV2 replay = makeTrace(action);
  uint32_t actionAgain = 0;
  const PreferenceLearnerV2 replayAgain = makeTrace(actionAgain);
  TEST_ASSERT_TRUE(action != 255);
  TEST_ASSERT_TRUE(actionAgain != 255);
  TEST_ASSERT_EQUAL_UINT32(replay.fingerprint(), replayAgain.fingerprint());
  TEST_ASSERT_EQUAL_UINT32(action, actionAgain);
}

void prequential_score_beats_uniform_after_feedback() {
  PreferenceLearnerV2 learner;
  PrequentialEvaluator evaluator;
  Pcg32 random(21, 34);

  const LearningDecision first = learner.choose(0, 1, 10, random);
  TEST_ASSERT_TRUE(evaluator.score(learner, first, true));
  TEST_ASSERT_TRUE(learner.feedback(first, true, 1, 20));

  const LearningDecision second = learner.choose(0, 2, 30, random);
  TEST_ASSERT_TRUE(evaluator.score(learner, second, true));
  TEST_ASSERT_TRUE(evaluator.metrics().samples == 2);
  TEST_ASSERT_TRUE(evaluator.metrics().brierGainQ32() > 0);
  TEST_ASSERT_TRUE(evaluator.metrics().accuracyQ16() >= 32768U);
}

void prequential_rejects_invalid_decisions() {
  PreferenceLearnerV2 learner;
  PrequentialEvaluator evaluator;
  LearningDecision invalid;
  invalid.id = 1;
  invalid.context = 9;
  TEST_ASSERT_FALSE(evaluator.score(learner, invalid, true));
  TEST_ASSERT_EQUAL_UINT32(0, evaluator.metrics().samples);
}
void shadow_keeps_baseline_and_candidate_in_step() {
  LearningShadow shadow(42, 54);
  const ShadowDecision first = shadow.choose(0, 1, 10);
  TEST_ASSERT_EQUAL_UINT32(1, shadow.metrics().decisions);
  TEST_ASSERT_TRUE(shadow.feedback(first, true, 1, 20));
  TEST_ASSERT_EQUAL_UINT32(1, shadow.baseline().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, shadow.candidate().metrics().feedback);

  TEST_ASSERT_FALSE(shadow.feedback(first, true, 1, 21));
  TEST_ASSERT_EQUAL_UINT32(1, shadow.baseline().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, shadow.metrics().feedbackRejected);
}

void shadow_replay_is_deterministic() {
  LearningShadow first(9, 12);
  LearningShadow second(9, 12);
  for (uint32_t id = 1; id <= 6; ++id) {
    const ShadowDecision a = first.choose(id % 2, id, id * 10);
    const ShadowDecision b = second.choose(id % 2, id, id * 10);
    TEST_ASSERT_EQUAL_UINT8(a.baselineAction, b.baselineAction);
    TEST_ASSERT_EQUAL_UINT8(a.candidate.action, b.candidate.action);
    TEST_ASSERT_TRUE(first.feedback(a, id % 3 != 0, 1, id * 10 + 1));
    TEST_ASSERT_TRUE(second.feedback(b, id % 3 != 0, 1, id * 10 + 1));
  }
  TEST_ASSERT_EQUAL_UINT32(first.fingerprint(), second.fingerprint());
  TEST_ASSERT_EQUAL_UINT32(first.metrics().agreements,
                           second.metrics().agreements);
}
void trace_runner_replays_shadow_and_scores_online() {
  const LearningTraceEvent trace[] = {
      {LearningTraceKind::Choose, 10, 1, 0, false, 0},
      {LearningTraceKind::Feedback, 20, 1, 0, true, 1},
      {LearningTraceKind::Choose, 30, 2, 0, false, 0},
      {LearningTraceKind::Feedback, 40, 2, 0, true, 1},
      {LearningTraceKind::Feedback, 41, 2, 0, true, 1},
      {LearningTraceKind::Feedback, 50, 99, 0, true, 1},
  };
  LearningTraceRunner runner;
  LearningTraceReport report;
  TEST_ASSERT_TRUE(runner.run(trace, 6, report, 42, 54));
  TEST_ASSERT_EQUAL_UINT32(6, report.processed);
  TEST_ASSERT_EQUAL_UINT32(1, report.unknownFeedback);
  TEST_ASSERT_EQUAL_UINT32(2, report.evaluation.samples);
  TEST_ASSERT_EQUAL_UINT32(2, report.shadow.feedbackAccepted);
  TEST_ASSERT_EQUAL_UINT32(1, report.shadow.feedbackRejected);
  TEST_ASSERT_TRUE(report.evaluation.brierGainQ32() > 0);
}

void trace_runner_replay_has_same_fingerprint() {
  const LearningTraceEvent trace[] = {
      {LearningTraceKind::Choose, 10, 1, 1, false, 0},
      {LearningTraceKind::Feedback, 20, 1, 1, false, 2},
      {LearningTraceKind::Choose, 30, 2, 1, false, 0},
      {LearningTraceKind::Feedback, 40, 2, 1, true, 2},
  };
  LearningTraceRunner runner;
  LearningTraceReport first;
  LearningTraceReport second;
  TEST_ASSERT_TRUE(runner.run(trace, 4, first, 7, 11));
  TEST_ASSERT_TRUE(runner.run(trace, 4, second, 7, 11));
  TEST_ASSERT_EQUAL_UINT32(first.fingerprint, second.fingerprint);
  TEST_ASSERT_TRUE(first.evaluation.brierQ32 ==
                  second.evaluation.brierQ32);
}
void explanation_reports_exploration_and_cells() {
  PreferenceLearnerV2 learner;
  Pcg32 random(2, 5);
  const LearningDecision decision = learner.choose(0, 1, 10, random);
  const LearningExplanation explanation =
      LearningExplainer::explain(learner, decision);
  TEST_ASSERT_TRUE(explanation.valid);
  TEST_ASSERT_EQUAL_UINT8(2, explanation.actionCount);
  TEST_ASSERT_EQUAL_UINT8(decision.action, explanation.selectedAction);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LearningReason::Exploration),
                          static_cast<uint8_t>(explanation.reason));
  TEST_ASSERT_TRUE(explanation.cells[decision.action].selected);
  TEST_ASSERT_EQUAL_UINT16(0, explanation.cells[decision.action].count);
}

void explanation_reports_posterior_winner() {
  PreferenceLearnerV2 learner;
  Pcg32 random(3, 7);
  for (uint32_t id = 1; id <= 4; ++id) {
    LearningDecision decision;
    decision.id = id;
    decision.context = 0;
    decision.action = 0;
    TEST_ASSERT_TRUE(learner.feedback(decision, false, 1, id + 10));
  }
  for (uint32_t id = 5; id <= 8; ++id) {
    LearningDecision decision;
    decision.id = id;
    decision.context = 0;
    decision.action = 1;
    TEST_ASSERT_TRUE(learner.feedback(decision, true, 1, id + 10));
  }
  const LearningDecision decision = learner.choose(0, 9, 100, random);
  const LearningExplanation explanation =
      LearningExplainer::explain(learner, decision);
  TEST_ASSERT_TRUE(explanation.valid);
  TEST_ASSERT_EQUAL_UINT8(1, explanation.selectedAction);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LearningReason::HighestPosterior),
                          static_cast<uint8_t>(explanation.reason));
  TEST_ASSERT_TRUE(explanation.marginQ16 > 0);
}

void explanation_rejects_invalid_decision() {
  PreferenceLearnerV2 learner;
  LearningDecision decision;
  decision.id = 7;
  decision.context = 8;
  const LearningExplanation explanation =
      LearningExplainer::explain(learner, decision);
  TEST_ASSERT_FALSE(explanation.valid);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LearningReason::Invalid),
                          static_cast<uint8_t>(explanation.reason));
}
void trace_dsl_parses_learning_events() {
  const LearningTraceParseResult choose =
      LearningTraceDslParser::parse("@+10 choose 42 1 # play context");
  TEST_ASSERT_TRUE(choose.accepted);
  TEST_ASSERT_EQUAL_UINT32(10, choose.command.atMs);
  TEST_ASSERT_EQUAL_UINT32(42, choose.command.value0);
  TEST_ASSERT_EQUAL_UINT32(1, choose.command.value1);
  LearningTraceEvent chooseEvent;
  TEST_ASSERT_TRUE(LearningTraceDslParser::toLearningEvent(
      choose.command, chooseEvent));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LearningTraceKind::Choose),
                          static_cast<uint8_t>(chooseEvent.kind));

  const LearningTraceParseResult feedback =
      LearningTraceDslParser::parse("@+20 feedback 42 positive 2");
  TEST_ASSERT_TRUE(feedback.accepted);
  TEST_ASSERT_TRUE(feedback.command.flag);
  LearningTraceEvent feedbackEvent;
  TEST_ASSERT_TRUE(LearningTraceDslParser::toLearningEvent(
      feedback.command, feedbackEvent));
  TEST_ASSERT_TRUE(feedbackEvent.approved);
  TEST_ASSERT_EQUAL_UINT8(2, feedbackEvent.source);
}

void trace_dsl_parses_extended_commands() {
  const LearningTraceParseResult command =
      LearningTraceDslParser::parse("@+3 cmd 0x1a2b");
  TEST_ASSERT_TRUE(command.accepted);
  TEST_ASSERT_EQUAL_UINT32(0x1a2b, command.command.value0);

  const LearningTraceParseResult sync =
      LearningTraceDslParser::parse("@+4 sync 12345 -420");
  TEST_ASSERT_TRUE(sync.accepted);
  TEST_ASSERT_EQUAL_INT32(-420, sync.command.signedValue);

  const LearningTraceParseResult expect =
      LearningTraceDslParser::parse("@+5 expect learner.margin >= 10");
  TEST_ASSERT_TRUE(expect.accepted);
  TEST_ASSERT_EQUAL_STRING("learner.margin", expect.command.text);
  TEST_ASSERT_EQUAL_STRING(">=", expect.command.op);
  TEST_ASSERT_EQUAL_UINT32(10, expect.command.value0);
}

void trace_dsl_rejects_malformed_lines() {
  const LearningTraceParseResult missingPrefix =
      LearningTraceDslParser::parse("choose 1 0");
  TEST_ASSERT_FALSE(missingPrefix.accepted);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LearningTraceParseError::MissingPrefix),
      static_cast<uint8_t>(missingPrefix.error));

  const LearningTraceParseResult unknown =
      LearningTraceDslParser::parse("@+1 dance 1");
  TEST_ASSERT_FALSE(unknown.accepted);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LearningTraceParseError::UnknownVerb),
      static_cast<uint8_t>(unknown.error));

  const LearningTraceParseResult trailing =
      LearningTraceDslParser::parse("@+1 reboot now");
  TEST_ASSERT_FALSE(trailing.accepted);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(LearningTraceParseError::TrailingArgument),
      static_cast<uint8_t>(trailing.error));

  const LearningTraceParseResult comment =
      LearningTraceDslParser::parse("  # comment only");
  TEST_ASSERT_FALSE(comment.accepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LearningTraceParseError::Ignored),
                          static_cast<uint8_t>(comment.error));
}

void trace_dsl_events_replay_through_runner() {
  const LearningTraceParseResult choose =
      LearningTraceDslParser::parse("@+10 choose 7 0");
  const LearningTraceParseResult feedback =
      LearningTraceDslParser::parse("@+20 feedback 7 approved 1");
  LearningTraceEvent events[2];
  TEST_ASSERT_TRUE(LearningTraceDslParser::toLearningEvent(choose.command,
                                                            events[0]));
  TEST_ASSERT_TRUE(LearningTraceDslParser::toLearningEvent(feedback.command,
                                                            events[1]));
  LearningTraceRunner runner;
  LearningTraceReport report;
  TEST_ASSERT_TRUE(runner.run(events, 2, report, 12, 15));
  TEST_ASSERT_EQUAL_UINT32(1, report.shadow.feedbackAccepted);
  TEST_ASSERT_EQUAL_UINT32(1, report.evaluation.samples);
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(starts_uncertain_and_explores);
  RUN_TEST(feedback_is_contextual_and_idempotent);
  RUN_TEST(positive_feedback_changes_future_policy);
  RUN_TEST(context_does_not_leak);
  RUN_TEST(evidence_is_bounded_and_replay_is_deterministic);
  RUN_TEST(prequential_score_beats_uniform_after_feedback);
  RUN_TEST(prequential_rejects_invalid_decisions);
  RUN_TEST(shadow_keeps_baseline_and_candidate_in_step);
  RUN_TEST(shadow_replay_is_deterministic);
  RUN_TEST(trace_runner_replays_shadow_and_scores_online);
  RUN_TEST(trace_runner_replay_has_same_fingerprint);
  RUN_TEST(explanation_reports_exploration_and_cells);
  RUN_TEST(explanation_reports_posterior_winner);
  RUN_TEST(explanation_rejects_invalid_decision);
  RUN_TEST(trace_dsl_parses_learning_events);
  RUN_TEST(trace_dsl_parses_extended_commands);
  RUN_TEST(trace_dsl_rejects_malformed_lines);
  RUN_TEST(trace_dsl_events_replay_through_runner);
  return UNITY_END();
}
