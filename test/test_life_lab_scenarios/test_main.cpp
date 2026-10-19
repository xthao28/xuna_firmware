#include <cstring>

#include <unity.h>

#include <xuna/lab/LifeScenarios.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

namespace {

void report(const char* name, const ScenarioReport& r) {
  if (!r.passed) {
    char message[96];
    snprintf(message, sizeof(message), "%s failed after %llu steps: %s", name,
             static_cast<unsigned long long>(r.steps), r.failure);
    TEST_FAIL_MESSAGE(message);
  }
}

}  // namespace

// S19b D: no feedback for 100 days leaves the learner bit-identical to fresh.
void scenario_d_no_feedback_for_100_days() {
  const ScenarioReport r = LifeScenarios::noFeedback(100);
  report("D", r);
  TEST_ASSERT_EQUAL_UINT32(0, r.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(800, r.suggestions);
  TEST_ASSERT_TRUE(r.checkpoints > 28000);
  TEST_ASSERT_TRUE(r.activeSeconds >= 100U * 86400U - 300U);
}

// S19b E: 10 reboots + 5 power cuts while rating.
void scenario_e_reboots_and_power_cuts() {
  const ScenarioReport r = LifeScenarios::rebootsAndPowerCuts(
      xuna_fixture::scripted, LifeEngine::kSnapshotSize);
  report("E", r);
  TEST_ASSERT_EQUAL_UINT32(16, r.boots);
  TEST_ASSERT_EQUAL_UINT32(5, r.powerCuts);
  TEST_ASSERT_TRUE(r.feedbackAcked > 40);
  TEST_ASSERT_EQUAL_UINT32(15, r.duplicatesRejected);
  TEST_ASSERT_TRUE(r.maxAgeLossSeconds <= 301);
}

// S19b stability: 100 days, invariants after every step.
void scenario_stability_100_days() {
  const ScenarioReport r = LifeScenarios::stability(xuna_fixture::scripted,
                                                    LifeEngine::kSnapshotSize, 100);
  report("stability", r);
  TEST_ASSERT_TRUE(r.feedbackAcked > 0);
  TEST_ASSERT_EQUAL_UINT32(10, r.boots);
}

// Same seed, same outcome.
void scenarios_are_repeatable() {
  const ScenarioReport a = LifeScenarios::stability(
      xuna_fixture::scripted, LifeEngine::kSnapshotSize, 20, 7);
  const ScenarioReport b = LifeScenarios::stability(
      xuna_fixture::scripted, LifeEngine::kSnapshotSize, 20, 7);
  report("stability-20", a);
  TEST_ASSERT_EQUAL_UINT32(a.feedbackAcked, b.feedbackAcked);
  TEST_ASSERT_EQUAL_UINT32(a.feedbackCount, b.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(a.activeSeconds, b.activeSeconds);
  TEST_ASSERT_EQUAL_UINT32(a.checkpoints, b.checkpoints);
}

// BootSequence characterization (S19b refactor of main.cpp setup()).
void boot_migrates_v1_and_keeps_the_pet_id() {
  BoardLab lab;
  lab.store().seedV1(xuna_fixture::scripted, LifeEngine::kSnapshotSize);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Migrated),
                          uint8_t(lab.lastBoot().recovery.result));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(IdentityState::Ready),
                          uint8_t(lab.identity().state()));
  uint8_t petId[16];
  std::memcpy(petId, lab.identity().record().petId, 16);
  TEST_ASSERT_EQUAL_UINT8(0x40, petId[6] & 0xF0);  // UUIDv4
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok),
                          uint8_t(lab.lastBoot().recovery.result));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(petId, lab.identity().record().petId, 16);
  TEST_ASSERT_EQUAL_UINT32(2, lab.lastBoot().boot);
  TEST_ASSERT_EQUAL_UINT32(2, lab.snapshot().counters.bootCounter);
}

// Known gap (S19b finding, owner decision pending): an unhatched device
// (no v1, no identity) saves v2 snapshots, and the next boot treats
// "snapshot Ok + identity Missing" as damage and stays read-only.
void boot_unhatched_device_is_read_only_after_saving() {
  BoardLab lab;
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(IdentityState::Egg),
                          uint8_t(lab.identity().state()));
  uint8_t frame[16];
  TEST_ASSERT_EQUAL_UINT8(0, lab.command(frame, BoardLab::suggest(frame, 1, 0), 1000));
  TEST_ASSERT_EQUAL_UINT8(0,
                          lab.command(frame, BoardLab::feedback(frame, 2, 1, true), 2000));
  TEST_ASSERT_FALSE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok),
                          uint8_t(lab.lastBoot().recovery.result));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(scenario_d_no_feedback_for_100_days);
  RUN_TEST(scenario_e_reboots_and_power_cuts);
  RUN_TEST(scenario_stability_100_days);
  RUN_TEST(scenarios_are_repeatable);
  RUN_TEST(boot_migrates_v1_and_keeps_the_pet_id);
  RUN_TEST(boot_unhatched_device_is_read_only_after_saving);
  return UNITY_END();
}
