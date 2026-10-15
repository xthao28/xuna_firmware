#include <unity.h>
#include <xuna/learning/BoardLearner.h>

using namespace xuna;

namespace {

// Fake durable store standing in for LifeStorage::save.
struct FakeStore : ILifeCommit {
  bool fail = false;
  uint32_t commits = 0;
  LifeEngine stored;
  bool commit(LifeEngine& staged) override {
    if (fail) return false;
    ++staged.generation;
    stored = staged;
    ++commits;
    return true;
  }
};

}  // namespace

void restore_uses_v1_history() {
  LifeEngine life;
  life.positive[1][1] = 6;  // play → happy approved
  life.negative[0][1] = 4;  // quiet → happy rejected
  BoardLearner learner(1);
  learner.restore(life);
  TEST_ASSERT_EQUAL_UINT8(1, learner.preferred(1));
  TEST_ASSERT_EQUAL_UINT8(0, learner.preferred(0));
}

void feedback_commits_first_then_applies_both() {
  LifeEngine life;
  BoardLearner learner(1);
  learner.restore(life);
  FakeStore store;
  const uint8_t shown = learner.choose(1);
  learner.accept();
  TEST_ASSERT_EQUAL(BoardLearner::Result::Applied,
                    learner.feedback(life, true, 1, store));
  TEST_ASSERT_EQUAL_UINT32(1, store.commits);
  TEST_ASSERT_EQUAL_UINT32(1, life.eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, life.generation);
  TEST_ASSERT_EQUAL_UINT16(1, life.positive[1][shown]);
  TEST_ASSERT_EQUAL_UINT16(1, learner.learner().view(1, 0, shown).rated);
  // One rating per decision.
  TEST_ASSERT_EQUAL(BoardLearner::Result::Rejected,
                    learner.feedback(life, true, 1, store));
  TEST_ASSERT_EQUAL_UINT32(1, store.commits);
}

void failed_commit_changes_nothing_and_retry_works() {
  LifeEngine life;
  BoardLearner learner(1);
  learner.restore(life);
  FakeStore store;
  learner.choose(0);
  learner.accept();
  const uint32_t before = learner.learner().fingerprint();
  store.fail = true;
  TEST_ASSERT_EQUAL(BoardLearner::Result::CommitFailed,
                    learner.feedback(life, true, 1, store));
  TEST_ASSERT_EQUAL_UINT32(0, life.eventCount);
  TEST_ASSERT_EQUAL_UINT32(before, learner.learner().fingerprint());
  TEST_ASSERT_TRUE(learner.hasPending());
  store.fail = false;
  TEST_ASSERT_EQUAL(BoardLearner::Result::Applied,
                    learner.feedback(life, true, 1, store));
  TEST_ASSERT_EQUAL_UINT32(1, life.eventCount);
}

void unaccepted_choice_and_bad_source_are_rejected() {
  LifeEngine life;
  BoardLearner learner(1);
  learner.restore(life);
  FakeStore store;
  learner.choose(0);  // refused by ActionGate: never accepted
  TEST_ASSERT_EQUAL(BoardLearner::Result::Rejected,
                    learner.feedback(life, true, 1, store));
  learner.accept();
  TEST_ASSERT_EQUAL(BoardLearner::Result::Rejected,
                    learner.feedback(life, true, 0, store));
  TEST_ASSERT_EQUAL(BoardLearner::Result::Rejected,
                    learner.feedback(life, true, 3, store));
  learner.invalidate();
  TEST_ASSERT_EQUAL(BoardLearner::Result::Rejected,
                    learner.feedback(life, true, 1, store));
  TEST_ASSERT_EQUAL_UINT32(0, store.commits);
}

void forget_resets_the_learner() {
  LifeEngine life;
  life.positive[1][1] = 9;
  BoardLearner learner(1);
  learner.restore(life);
  learner.forget();
  TEST_ASSERT_EQUAL_UINT32(
      PreferenceLearnerV3(v1CompatibleLearnerConfig()).fingerprint(),
      learner.learner().fingerprint());
  TEST_ASSERT_FALSE(learner.hasPending());
}

void v3_follows_a_change_that_v1_does_not() {
  LifeEngine life;
  life.positive[1][1] = 60;  // long history: happy at play
  BoardLearner learner(1);
  learner.restore(life);
  FakeStore store;
  TEST_ASSERT_EQUAL_UINT8(1, learner.preferred(1));
  // The owner now rejects happy and approves idle at play.
  uint32_t ratings = 0;
  while (learner.preferred(1) == 1 && ratings < 60) {
    const uint8_t shown = learner.choose(1);
    learner.accept();
    learner.feedback(life, shown == 0, 1, store);
    ++ratings;
  }
  TEST_ASSERT_EQUAL_UINT8(0, learner.preferred(1));
  TEST_ASSERT_TRUE(ratings <= 20);
  // v1, on the same durable counts, still prefers happy.
  TEST_ASSERT_EQUAL_UINT8(1, life.choose(1));
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(restore_uses_v1_history);
  RUN_TEST(feedback_commits_first_then_applies_both);
  RUN_TEST(failed_commit_changes_nothing_and_retry_works);
  RUN_TEST(unaccepted_choice_and_bad_source_are_rejected);
  RUN_TEST(forget_resets_the_learner);
  RUN_TEST(v3_follows_a_change_that_v1_does_not);
  return UNITY_END();
}
