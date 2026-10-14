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
  TEST_ASSERT_TRUE(lab.persist());
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(1,
                           lab.session().core().candidate().metrics().feedback);

  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
  TEST_ASSERT_EQUAL_UINT32(0,
                           lab.session().core().candidate().metrics().feedback);
}

void snapshot_lab_power_cut_keeps_previous_committed_slot() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  const LifeCoreEvent choose1{LifeCoreEventKind::Choose, 1, 0, 0, false, 0,
                              0};
  const LifeCoreEvent feedback1{LifeCoreEventKind::Feedback, 1, 0, 0, true, 1,
                                0};
  lab.session().enqueue(choose1);
  lab.session().step(10);
  lab.session().enqueue(feedback1);
  lab.session().step(20);
  TEST_ASSERT_TRUE(lab.persist());

  const LifeCoreEvent choose2{LifeCoreEventKind::Choose, 2, 0, 0, false, 0,
                              0};
  const LifeCoreEvent feedback2{LifeCoreEventKind::Feedback, 2, 0, 0, true, 1,
                                0};
  lab.session().enqueue(choose2);
  lab.session().step(30);
  lab.session().enqueue(feedback2);
  lab.session().step(40);
  lab.powerCutNextPersist();
  TEST_ASSERT_FALSE(lab.persist());
  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL_UINT32(1, lab.session().core().life().eventCount);
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
  RUN_TEST(snapshot_lab_recovery_rejects_corrupt_latest_slot);
  return UNITY_END();
}