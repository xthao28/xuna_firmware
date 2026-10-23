#include <unity.h>
#include <xuna/lab/LearnerBench.h>
#include <xuna/lab/OwnerModel.h>

using namespace xuna;

namespace {

OwnerConfig oracle() {
  OwnerConfig config;
  config.kind = OwnerKind::Oracle;
  config.preferred[0] = 1;
  config.preferred[1] = 0;
  return config;
}

}  // namespace

void owner_probabilities_follow_scenario() {
  OwnerModel fixed(oracle());
  TEST_ASSERT_EQUAL_UINT16(58982, fixed.approveQ16(0, 0, 1));
  TEST_ASSERT_EQUAL_UINT16(13107, fixed.approveQ16(0, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(1, fixed.bestAction(0, 0));
  TEST_ASSERT_EQUAL_UINT8(0, fixed.bestAction(0, 1));
  TEST_ASSERT_EQUAL_UINT16(0, fixed.approveQ16(0, 2, 0));

  OwnerConfig reversal = oracle();
  reversal.kind = OwnerKind::Reversal;
  reversal.laterPreferred[0] = 0;
  reversal.changeAt = 60;
  OwnerModel switched(reversal);
  TEST_ASSERT_EQUAL_UINT8(1, switched.bestAction(59, 0));
  TEST_ASSERT_EQUAL_UINT8(0, switched.bestAction(60, 0));

  OwnerConfig drift = oracle();
  drift.kind = OwnerKind::Drift;
  drift.laterPreferred[0] = 0;
  drift.changeAt = 10;
  drift.driftLength = 100;
  OwnerModel moving(drift);
  TEST_ASSERT_EQUAL_UINT16(58982, moving.approveQ16(10, 0, 1));
  const uint16_t middle = moving.approveQ16(60, 0, 1);
  TEST_ASSERT_TRUE(middle < 58982 && middle > 13107);
  TEST_ASSERT_EQUAL_UINT16(13107, moving.approveQ16(110, 0, 1));
  TEST_ASSERT_EQUAL_UINT16(58982, moving.approveQ16(500, 0, 0));
}

void owner_sampling_is_deterministic_and_respects_rate() {
  OwnerConfig config = oracle();
  config.rateQ16 = 6554;  // about 10%
  OwnerModel owner(config);
  Pcg32 first(7, 11), second(7, 11);
  uint32_t rated = 0;
  for (uint32_t i = 0; i < 2000; ++i) {
    const bool a = owner.rates(first);
    TEST_ASSERT_EQUAL(a, owner.rates(second));
    if (a) ++rated;
  }
  TEST_ASSERT_TRUE(rated > 120 && rated < 280);

  config.kind = OwnerKind::NoFeedback;
  config.rateQ16 = 65535;
  OwnerModel silent(config);
  for (uint32_t i = 0; i < 100; ++i) TEST_ASSERT_FALSE(silent.rates(first));
}

void personality_owner_kinds_follow_their_definition() {
  OwnerConfig config = oracle();
  config.laterPreferred[0] = 2;
  config.actions = 3;
  config.kind = OwnerKind::Noisy;
  OwnerModel noisy(config);
  TEST_ASSERT_EQUAL_UINT16(39322, noisy.approveQ16(0, 0, 1));
  TEST_ASSERT_EQUAL_UINT16(26214, noisy.approveQ16(0, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(1, noisy.bestAction(0, 0));

  config.kind = OwnerKind::Random;
  OwnerModel random(config);
  for (uint8_t a = 0; a < 3; ++a)
    TEST_ASSERT_EQUAL_UINT16(32768, random.approveQ16(9, 0, a));
  TEST_ASSERT_EQUAL_UINT8(0, random.bestAction(9, 0));

  config.kind = OwnerKind::Teacher;
  config.rateQ16 = 0;
  OwnerModel teacher(config);
  Pcg32 draws(3, 7);
  for (uint32_t i = 0; i < 50; ++i) {
    TEST_ASSERT_TRUE(teacher.rates(draws));
    TEST_ASSERT_TRUE(teacher.approves(i, 0, 1, draws));
    TEST_ASSERT_FALSE(teacher.approves(i, 0, 2, draws));
  }

  config.kind = OwnerKind::Alternating;
  config.period = 3;
  OwnerModel alternating(config);
  TEST_ASSERT_EQUAL_UINT8(1, alternating.bestAction(2, 0));
  TEST_ASSERT_EQUAL_UINT8(2, alternating.bestAction(3, 0));
  TEST_ASSERT_EQUAL_UINT8(2, alternating.bestAction(5, 0));
  TEST_ASSERT_EQUAL_UINT8(1, alternating.bestAction(6, 0));
}

void sparse_owner_reacts_rarely() {
  OwnerConfig config = oracle();
  config.kind = OwnerKind::Sparse;
  config.rateQ16 = 65535;  // ignored: Sparse uses sparseRateQ16 (5%)
  OwnerModel owner(config);
  TEST_ASSERT_EQUAL_UINT16(58982, owner.approveQ16(0, 0, 1));
  Pcg32 draws(5, 1);
  uint32_t rated = 0;
  for (uint32_t i = 0; i < 4000; ++i)
    if (owner.rates(draws)) ++rated;
  TEST_ASSERT_TRUE(rated > 120 && rated < 280);
}

void bench_is_deterministic_per_seed() {
  OwnerModel owner(oracle());
  BenchConfig config;
  V2BenchLearner learner;
  const BenchResult a = LearnerBench::run(learner, owner, config, 42);
  const BenchResult b = LearnerBench::run(learner, owner, config, 42);
  TEST_ASSERT_TRUE(a.supported);
  TEST_ASSERT_EQUAL_UINT32(a.regretQ16, b.regretQ16);
  TEST_ASSERT_EQUAL_UINT32(a.rated, b.rated);
  TEST_ASSERT_EQUAL_UINT32(a.trialsToTarget, b.trialsToTarget);
  TEST_ASSERT_TRUE(a.brierQ32 == b.brierQ32);
}

void no_feedback_leaves_learners_fresh() {
  OwnerConfig config = oracle();
  config.kind = OwnerKind::NoFeedback;
  OwnerModel owner(config);
  BenchConfig bench;
  V1BenchLearner v1;
  V2BenchLearner v2;
  const BenchResult one = LearnerBench::run(v1, owner, bench, 3);
  const BenchResult two = LearnerBench::run(v2, owner, bench, 3);
  TEST_ASSERT_TRUE(one.stateUnchanged);
  TEST_ASSERT_TRUE(two.stateUnchanged);
  TEST_ASSERT_EQUAL_UINT32(0, one.rated);
  TEST_ASSERT_EQUAL_UINT32(0, two.rated);
}

void static_baseline_regret_is_exact() {
  // Static always shows action 0: wrong in context 0 every second trial.
  OwnerModel owner(oracle());
  BenchConfig config;
  config.trials = 10;
  StaticBenchLearner learner;
  const BenchResult result = LearnerBench::run(learner, owner, config, 1);
  TEST_ASSERT_EQUAL_UINT32(5U * (58982U - 13107U), result.regretQ16);
}

void v1_cannot_run_four_actions() {
  OwnerConfig config = oracle();
  config.actions = 4;
  OwnerModel owner(config);
  V1BenchLearner v1;
  const BenchResult result = LearnerBench::run(v1, owner, BenchConfig(), 1);
  TEST_ASSERT_FALSE(result.supported);
}

void learner_beats_random_on_oracle() {
  OwnerModel owner(oracle());
  BenchConfig config;
  uint64_t v2Regret = 0, randomRegret = 0;
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    V2BenchLearner v2;
    RandomBenchLearner random;
    v2Regret += LearnerBench::run(v2, owner, config, seed).regretQ16;
    randomRegret += LearnerBench::run(random, owner, config, seed).regretQ16;
  }
  TEST_ASSERT_TRUE(v2Regret < randomRegret);
}

void greedy_curve_is_recorded() {
  OwnerModel owner(oracle());
  BenchConfig config;
  config.trials = 40;
  uint8_t curve[40] = {};
  V2BenchLearner learner;
  const BenchResult result =
      LearnerBench::run(learner, owner, config, 5, curve);
  uint32_t ones = 0;
  for (uint32_t i = 0; i < 40; ++i) {
    TEST_ASSERT_TRUE(curve[i] <= 1);
    ones += curve[i];
  }
  TEST_ASSERT_TRUE(ones > 0);
  TEST_ASSERT_TRUE(result.trialsToTarget != LearnerBench::kNever);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(owner_probabilities_follow_scenario);
  RUN_TEST(owner_sampling_is_deterministic_and_respects_rate);
  RUN_TEST(personality_owner_kinds_follow_their_definition);
  RUN_TEST(sparse_owner_reacts_rarely);
  RUN_TEST(bench_is_deterministic_per_seed);
  RUN_TEST(no_feedback_leaves_learners_fresh);
  RUN_TEST(static_baseline_regret_is_exact);
  RUN_TEST(v1_cannot_run_four_actions);
  RUN_TEST(learner_beats_random_on_oracle);
  RUN_TEST(greedy_curve_is_recorded);
  return UNITY_END();
}
