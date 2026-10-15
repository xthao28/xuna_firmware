#include <unity.h>

#include <xuna/life/LifeCoreSnapshotLab.h>

using namespace xuna;

void snapshot_lab_restores_feedback_and_resets_shadow_candidate() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, 1, 0, 0, false, 0,
                             0};
  lab.session().enqueue(choose);
  lab.session().step(10);
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, 1, 0, 0, true, 1,
                               0};
  lab.session().enqueue(feedback);
  const LifeCoreSessionStepOutput applied = lab.session().step(20);
  TEST_ASSERT_TRUE(applied.core.learningFeedbackApplied);
  // Committed before it was applied (§9.5); no separate persist needed.
  TEST_ASSERT_TRUE(applied.core.feedbackCommitted);
  TEST_ASSERT_FALSE(applied.core.persistRequested);
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().generation);
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1,
                           lab.session().core().shadowMetrics().feedbackAccepted);

  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(0,
                           lab.session().core().shadowMetrics().feedbackAccepted);
  // The v3 shadow candidate is not persisted yet and restarts fresh.
  TEST_ASSERT_EQUAL_UINT32(
      PreferenceLearnerV3(LifeCore::shadowConfig()).fingerprint(),
      lab.session().core().candidate().fingerprint());
}

namespace {

void chooseAndRate(LifeCoreSnapshotLab& lab, uint32_t id, MonoMs at) {
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, id, 0, 0, false, 0, 0};
  lab.session().enqueue(choose);
  lab.session().step(at);
}

LifeCoreSessionStepOutput rate(LifeCoreSnapshotLab& lab, uint32_t id,
                               MonoMs at) {
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, id, 0, 0, true, 1,
                               0};
  lab.session().enqueue(feedback);
  return lab.session().step(at);
}

}  // namespace

void snapshot_lab_power_cut_keeps_previous_committed_slot() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  chooseAndRate(lab, 1, 10);
  TEST_ASSERT_TRUE(rate(lab, 1, 20).core.feedbackCommitted);

  chooseAndRate(lab, 2, 3000);
  lab.powerCutNextPersist();
  TEST_ASSERT_TRUE(rate(lab, 2, 3010).core.feedbackNotReady);
  TEST_ASSERT_TRUE(lab.reboot(4000));
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
}

void failed_commit_leaves_ram_unchanged_and_allows_retry() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  chooseAndRate(lab, 1, 10);
  rate(lab, 1, 20);
  const uint32_t candidateBefore =
      lab.session().core().candidate().fingerprint();

  chooseAndRate(lab, 2, 3000);
  lab.powerCutNextPersist();
  const LifeCoreSessionStepOutput failed = rate(lab, 2, 3010);
  TEST_ASSERT_TRUE(failed.core.feedbackNotReady);
  TEST_ASSERT_FALSE(failed.core.learningFeedbackApplied);
  TEST_ASSERT_FALSE(failed.core.feedbackCommitted);
  // v1 and the v3 shadow are untouched; the decision is still pending.
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().generation);
  TEST_ASSERT_EQUAL_UINT32(candidateBefore,
                           lab.session().core().candidate().fingerprint());
  TEST_ASSERT_TRUE(failed.core.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(2, failed.core.decisionId);
  TEST_ASSERT_EQUAL_UINT32(1,
                           lab.session().core().shadowMetrics().commitFailures);

  // Retry of the same feedback now commits and applies exactly once.
  const LifeCoreSessionStepOutput retried = rate(lab, 2, 3020);
  TEST_ASSERT_TRUE(retried.core.feedbackCommitted);
  TEST_ASSERT_TRUE(retried.core.learningFeedbackApplied);
  TEST_ASSERT_EQUAL_UINT32(2, lab.session().core().life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(2, lab.session().core().life().generation);
  TEST_ASSERT_FALSE(rate(lab, 2, 3030).core.learningFeedbackApplied);
  TEST_ASSERT_TRUE(lab.reboot(5000));
  TEST_ASSERT_EQUAL_UINT32(2, lab.session().core().life().eventCount);
}

void snapshot_lab_recovery_rejects_corrupt_latest_slot() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_TRUE(lab.persist());
  const uint32_t firstGeneration = lab.session().core().life().generation;
  lab.session().core().life().eventCount = 4;
  TEST_ASSERT_TRUE(lab.persist());
  TEST_ASSERT_TRUE(lab.slotValid(0));
  TEST_ASSERT_TRUE(lab.slotValid(1));
  lab.corruptSlot(lab.slotGeneration(0) > lab.slotGeneration(1) ? 0 : 1);
  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL_UINT32(firstGeneration,
                           lab.session().core().life().generation);
  TEST_ASSERT_EQUAL_UINT32(0, lab.session().core().life().eventCount);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(snapshot_lab_restores_feedback_and_resets_shadow_candidate);
  RUN_TEST(snapshot_lab_power_cut_keeps_previous_committed_slot);
  RUN_TEST(failed_commit_leaves_ram_unchanged_and_allows_retry);
  RUN_TEST(snapshot_lab_recovery_rejects_corrupt_latest_slot);
  return UNITY_END();
}