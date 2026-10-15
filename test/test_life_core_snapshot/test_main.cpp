#include <unity.h>

#include <xuna/life/LifeCoreSnapshotLab.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

namespace {

uint8_t chooseAndShow(LifeCoreSnapshotLab& lab, uint32_t id, MonoMs at,
                      uint8_t context = 0) {
  const LifeCoreEvent choose{LifeCoreEventKind::Choose, id, context, 0, false,
                             0, 0};
  lab.session().enqueue(choose);
  return lab.session().step(at).core.selectedAction;
}

LifeCoreSessionStepOutput rate(LifeCoreSnapshotLab& lab, uint32_t id,
                               MonoMs at, bool approved = true) {
  const LifeCoreEvent feedback{LifeCoreEventKind::Feedback, id, 0, 0, approved,
                               1, 0};
  lab.session().enqueue(feedback);
  return lab.session().step(at);
}

const LifeSnapshotV2& snap(LifeCoreSnapshotLab& lab) {
  return lab.session().core().snapshot();
}

}  // namespace

// Unlike the v1 era, learning survives a reboot exactly: the discounted v3
// masses are stored, not rebuilt from undiscounted counts.
void reboot_restores_learner_exactly() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL(int(LoadResult::Missing), int(lab.lastRecovery().result));
  for (uint32_t id = 1; id <= 5; ++id) {
    chooseAndShow(lab, id, id * 4000U, id % 2);
    const LifeCoreSessionStepOutput out = rate(lab, id, id * 4000U + 100U, id != 3);
    TEST_ASSERT_TRUE(out.core.feedbackCommitted);
    TEST_ASSERT_FALSE(out.core.persistRequested);
  }
  const uint32_t learned = lab.session().core().learner().fingerprint();
  TEST_ASSERT_EQUAL_UINT32(5, snap(lab).generation);

  TEST_ASSERT_TRUE(lab.reboot(60000));
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(lab.lastRecovery().result));
  TEST_ASSERT_EQUAL_UINT32(learned, lab.session().core().learner().fingerprint());
  TEST_ASSERT_EQUAL_UINT32(5, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(5, snap(lab).episodes.count);
  // Pending decisions never survive a reboot (plan §8.4).
  TEST_ASSERT_FALSE(lab.session().step(60010).core.hasDecision);
}

void power_cut_keeps_previous_committed_state() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  chooseAndShow(lab, 1, 10);
  TEST_ASSERT_TRUE(rate(lab, 1, 20).core.feedbackCommitted);
  const uint32_t committed = lab.session().core().learner().fingerprint();

  chooseAndShow(lab, 2, 3000);
  lab.powerCutNextPersist();
  TEST_ASSERT_TRUE(rate(lab, 2, 3010).core.feedbackNotReady);
  TEST_ASSERT_TRUE(lab.reboot(4000));
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(lab.lastRecovery().result));
  TEST_ASSERT_EQUAL_UINT8(1, lab.lastRecovery().corruptSlots);
  TEST_ASSERT_EQUAL_UINT32(1, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(committed, lab.session().core().learner().fingerprint());
}

void failed_commit_leaves_ram_unchanged_and_allows_retry() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  chooseAndShow(lab, 1, 10);
  rate(lab, 1, 20);
  const uint32_t before = lab.session().core().learner().fingerprint();

  chooseAndShow(lab, 2, 3000);
  lab.powerCutNextPersist();
  const LifeCoreSessionStepOutput failed = rate(lab, 2, 3010);
  TEST_ASSERT_TRUE(failed.core.feedbackNotReady);
  TEST_ASSERT_FALSE(failed.core.learningFeedbackApplied);
  TEST_ASSERT_FALSE(failed.core.feedbackCommitted);
  TEST_ASSERT_EQUAL_UINT32(1, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(1, snap(lab).generation);
  TEST_ASSERT_EQUAL_UINT32(before, lab.session().core().learner().fingerprint());
  TEST_ASSERT_TRUE(failed.core.hasDecision);
  TEST_ASSERT_EQUAL_UINT32(2, failed.core.decisionId);

  // Retry of the same feedback now commits and applies exactly once.
  const LifeCoreSessionStepOutput retried = rate(lab, 2, 3020);
  TEST_ASSERT_TRUE(retried.core.feedbackCommitted);
  TEST_ASSERT_TRUE(retried.core.learningFeedbackApplied);
  TEST_ASSERT_EQUAL_UINT32(2, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(2, snap(lab).generation);
  TEST_ASSERT_FALSE(rate(lab, 2, 3030).core.learningFeedbackApplied);
  TEST_ASSERT_TRUE(lab.reboot(5000));
  TEST_ASSERT_EQUAL_UINT32(2, snap(lab).counters.feedbackCount);
}

// Plan §9.9 "Spam": 1,000 feedback attempts in 60 s apply at most once per
// reaction and cause exactly one urgent commit.
void feedback_spam_applies_once_and_commits_once() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  chooseAndShow(lab, 1, 10);
  uint32_t applied = 0, committed = 0;
  for (uint32_t i = 0; i < 1000; ++i) {
    const LifeCoreSessionStepOutput out =
        rate(lab, 1, 20 + static_cast<MonoMs>(i) * 60U);
    if (out.core.learningFeedbackApplied) ++applied;
    if (out.core.feedbackCommitted) ++committed;
  }
  TEST_ASSERT_EQUAL_UINT32(1, applied);
  TEST_ASSERT_EQUAL_UINT32(1, committed);
  TEST_ASSERT_EQUAL_UINT32(1, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(1, snap(lab).generation);
}

void recovery_skips_a_corrupt_latest_slot() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_TRUE(lab.persist());
  lab.session().core().snapshot().counters.activeSeconds = 77;
  TEST_ASSERT_TRUE(lab.persist());
  TEST_ASSERT_EQUAL_UINT32(2, snap(lab).generation);
  lab.store().corrupt(SnapshotKey::V2Slot0);  // generation 2 is in slot 0
  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(lab.lastRecovery().result));
  TEST_ASSERT_EQUAL_INT8(1, lab.lastRecovery().slot);
  TEST_ASSERT_EQUAL_UINT32(1, snap(lab).generation);
  TEST_ASSERT_EQUAL_UINT32(0, snap(lab).counters.activeSeconds);
}

// Upgrade path: an MVP pet's v1 snapshot migrates (E10-C) and keeps living;
// the first rating writes v2 while the v1 slot stays as the rollback point.
void v1_pet_upgrades_and_keeps_its_learning() {
  LifeCoreSnapshotLab lab(42, 54);
  lab.seedV1(xuna_fixture::scripted, LifeEngine::kSnapshotSize);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(lab.lastRecovery().result));
  TEST_ASSERT_EQUAL_UINT32(4, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT16(3, lab.session().core().learner().view(1, 0, 1).rated);
  TEST_ASSERT_EQUAL_UINT8(1, lab.session().core().preferred(LearnInteraction::PlayRequest));

  chooseAndShow(lab, 1, 10, 1);
  TEST_ASSERT_TRUE(rate(lab, 1, 20).core.feedbackCommitted);
  TEST_ASSERT_TRUE(lab.store().exists(SnapshotKey::V1Slot0));
  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(lab.lastRecovery().result));
  TEST_ASSERT_EQUAL_UINT32(5, snap(lab).counters.feedbackCount);
}

// Downgrade protection: an intact snapshot from a newer schema puts XUNA in
// ERROR_SAFE (read-only) instead of overwriting it.
void newer_schema_starts_read_only() {
  LifeCoreSnapshotLab lab(42, 54);
  uint8_t bytes[64] = {'X', 'U', 3, 0, 16, 0, 0, 0, 9, 0, 0, 0};
  const uint32_t crc = crc32(bytes, 12);
  for (int i = 0; i < 4; ++i) bytes[12 + i] = static_cast<uint8_t>(crc >> (8 * i));
  lab.store().put(SnapshotKey::V2Slot1, bytes, 16);
  lab.boot(0);
  TEST_ASSERT_EQUAL(int(LoadResult::TooNew), int(lab.lastRecovery().result));
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  lab.session().enqueue(greeting);
  const LifeCoreSessionStepOutput out = lab.session().step(10);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Activity::ErrorSafe),
                          static_cast<uint8_t>(out.core.activity));
  TEST_ASSERT_FALSE(out.core.decisionShown);
  TEST_ASSERT_FALSE(lab.session().core().forget());
  TEST_ASSERT_FALSE(lab.store().exists(SnapshotKey::V2Slot0));
}

// Forget writes the empty snapshot to both slots, so corrupting the newest
// one cannot bring old learning back.
void forget_reaches_both_slots() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  for (uint32_t id = 1; id <= 3; ++id) {
    chooseAndShow(lab, id, id * 4000U);
    rate(lab, id, id * 4000U + 100U);
  }
  TEST_ASSERT_TRUE(lab.session().core().forget());
  TEST_ASSERT_EQUAL_UINT32(5, snap(lab).generation);
  lab.store().corrupt(SnapshotKey::V2Slot1);  // generation 5
  TEST_ASSERT_TRUE(lab.reboot(20000));
  TEST_ASSERT_EQUAL_UINT32(4, snap(lab).generation);
  TEST_ASSERT_EQUAL_UINT32(0, snap(lab).counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(PreferenceLearnerV3(productLearnerConfig()).fingerprint(),
                           lab.session().core().learner().fingerprint());
}

// S10: after a reboot time is unknown again (no RTC), but the persisted
// watermark still rejects a phone clock that went back by more than 5 min.
void clock_watermark_survives_reboot_and_blocks_rollback() {
  const int64_t evening = ClockModel::kEpochUtcMs + 12LL * 3600000LL;
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(SyncStatus::Accepted),
      static_cast<uint8_t>(lab.session().sync(evening, 420, TimeSource::Phone, 10).status));
  TEST_ASSERT_TRUE(lab.persist());  // lazy commit carries the watermark
  const uint32_t watermark = snap(lab).clock.lastTrustedUtcMinute;

  TEST_ASSERT_TRUE(lab.reboot(1000));
  TEST_ASSERT_EQUAL_UINT32(watermark, snap(lab).clock.lastTrustedUtcMinute);
  const LifeCoreSessionStepOutput unknown = lab.session().step(1010);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ClockQuality::Unknown),
                          static_cast<uint8_t>(unknown.clockQuality));
  // Unknown time: a greeting is learned in the coarse cell only.
  const LifeCoreEvent greeting{LifeCoreEventKind::Greeting};
  lab.session().enqueue(greeting);
  TEST_ASSERT_EQUAL_UINT8(0, lab.session().step(1020).core.decisionDayPart);

  const SyncResult back = lab.session().sync(evening - 3600000LL, 420,
                                             TimeSource::Phone, 1030);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::RejectedRollback),
                          static_cast<uint8_t>(back.status));
  const SyncResult forward = lab.session().sync(evening + 600000LL, 420,
                                                TimeSource::Phone, 1040);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::Accepted),
                          static_cast<uint8_t>(forward.status));
  TEST_ASSERT_TRUE(forward.gap);  // offline time is flagged, never invented
}

// D3 with §9.5: a retry after a failed commit is still bound by the 120 s
// window; once it has passed, nothing is committed or applied.
void retry_after_the_window_expires_without_commit() {
  LifeCoreSnapshotLab lab(42, 54);
  TEST_ASSERT_TRUE(lab.boot(0));
  chooseAndShow(lab, 1, 1000);
  lab.powerCutNextPersist();
  TEST_ASSERT_TRUE(rate(lab, 1, 2000).core.feedbackNotReady);
  const LifeCoreSessionStepOutput late = rate(lab, 1, 1000 + 120001);
  TEST_ASSERT_TRUE(late.core.feedbackExpired);
  TEST_ASSERT_FALSE(late.core.feedbackCommitted);
  TEST_ASSERT_EQUAL_UINT32(0, snap(lab).counters.feedbackCount);
  TEST_ASSERT_TRUE(lab.reboot(200000));
  TEST_ASSERT_EQUAL_UINT32(0, snap(lab).counters.feedbackCount);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(reboot_restores_learner_exactly);
  RUN_TEST(power_cut_keeps_previous_committed_state);
  RUN_TEST(failed_commit_leaves_ram_unchanged_and_allows_retry);
  RUN_TEST(feedback_spam_applies_once_and_commits_once);
  RUN_TEST(recovery_skips_a_corrupt_latest_slot);
  RUN_TEST(v1_pet_upgrades_and_keeps_its_learning);
  RUN_TEST(newer_schema_starts_read_only);
  RUN_TEST(forget_reaches_both_slots);
  RUN_TEST(clock_watermark_survives_reboot_and_blocks_rollback);
  RUN_TEST(retry_after_the_window_expires_without_commit);
  return UNITY_END();
}
