#include <unity.h>

#include <xuna/life/LifeCoreSnapshotLab.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>

using namespace xuna;

// PE-4d (2026-10-25): the owner can undo XUNA's newest personality change or
// reset what it became (opcode 9). Commit-first; learning is untouched.

namespace {

const uint32_t kSeed = 0x5eed1234U;
const uint32_t kMinute = 60000;
const uint8_t kSociability = static_cast<uint8_t>(TraitId::Sociability);

struct Rig {
  MemorySnapshotStore store;
  StoreSnapshotCommit commit;
  LifeCore core;
  V1LifeCoreAdapter adapter;
  uint32_t id = 0x100;
  Rig() : commit(store), core(42, 54), adapter(core, commit, false, true) {}

  void boot(uint32_t now) {
    LifeSnapshotV2 snapshot;
    const RecoveryReport report = SnapshotStore::recover(store, snapshot);
    if (report.result == LoadResult::Ok || report.result == LoadResult::Migrated)
      core.restore(snapshot);
    core.seedPersonality(kSeed);
    core.begin(now, true);
    adapter.begin(now);
  }

  uint8_t send(uint8_t opcode, uint8_t arg, uint32_t now) {
    const uint8_t bytes[8] = {1, opcode, uint8_t(++id), uint8_t(id >> 8), 0, 0, arg, 0};
    uint32_t echoed = 0;
    return adapter.handle(bytes, 8, now, true, echoed);
  }

  // Four days of praise in chat: enough sessions for one sociability step.
  uint32_t praiseForDays(uint32_t now, uint32_t days) {
    for (uint32_t d = 0; d < days; ++d) {
      for (uint8_t i = 0; i < 5; ++i) send(8, 1, now += 1000);
      adapter.tick(now += 31 * kMinute);
      now += Personality::kDayMs;
    }
    // Section 5 is lazy: the board's 5-minute checkpoint writes it (main.cpp).
    TEST_ASSERT_TRUE(adapter.checkpoint(now));
    return now;
  }

  int16_t sociability() const { return core.personality().genome().offset[kSociability]; }
};

}  // namespace

void personality_undo_walks_back_through_the_journal() {
  Personality p(kSeed);
  p.begin(0);
  TEST_ASSERT_FALSE(p.undoLatest());  // nothing yet
  uint32_t now = 1000;
  for (uint32_t d = 0; d < 4; ++d, now += Personality::kDayMs) {
    p.ownerInput(now);
    for (uint8_t i = 0; i < 5; ++i) p.conversation(ConversationTag::Playful, now + i);
    p.step(now + 31 * kMinute, Activity::Idle, 40000);
  }
  const uint8_t play = static_cast<uint8_t>(TraitId::Playfulness);
  TEST_ASSERT_TRUE(p.genome().offset[play] > 0);
  const uint16_t sequence = p.ledger().journal(0).sequence;
  TEST_ASSERT_TRUE(sequence != 0);
  const uint32_t revision = p.revision();
  TEST_ASSERT_TRUE(p.undoLatest());
  TEST_ASSERT_EQUAL_INT16(0, p.genome().offset[play]);
  TEST_ASSERT_EQUAL_UINT16(0, p.ledger().journal(0).sequence);
  TEST_ASSERT_TRUE(p.revision() != revision);  // section 5 is re-encoded
  TEST_ASSERT_FALSE(p.undoLatest());
  TEST_ASSERT_EQUAL_UINT16(sequence, p.ledger().journalSequence());  // ids never reused
}

void undo_is_committed_first_and_survives_a_reboot() {
  Rig rig;
  rig.boot(1000);
  const uint32_t feedback = rig.core.snapshot().counters.feedbackCount;
  const uint8_t quiet = static_cast<uint8_t>(rig.adapter.preferred(0));
  uint32_t now = rig.praiseForDays(2000, 4);
  const int16_t moved = rig.sociability();
  TEST_ASSERT_TRUE(moved > 0);
  {  // a reboot before the undo keeps the change (the check below is not vacuous)
    Rig before;
    before.store = rig.store;
    before.boot(now);
    TEST_ASSERT_EQUAL_INT16(moved, before.sociability());
  }
  // Storage fault: not_ready and nothing changes.
  rig.store.powerCutNextWrite();
  TEST_ASSERT_EQUAL_UINT8(7, rig.send(9, 1, now += 1000));
  TEST_ASSERT_EQUAL_INT16(moved, rig.sociability());
  TEST_ASSERT_EQUAL_UINT8(0, rig.send(9, 1, now += 1000));
  TEST_ASSERT_EQUAL_INT16(0, rig.sociability());
  TEST_ASSERT_EQUAL_UINT8(4, rig.send(9, 1, now += 1000));  // nothing left to undo
  TEST_ASSERT_EQUAL_UINT32(feedback, rig.core.snapshot().counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(quiet, static_cast<uint8_t>(rig.adapter.preferred(0)));
  // A reboot finds the undone state, not the change.
  Rig after;
  after.store = rig.store;
  after.boot(now += 1000);
  TEST_ASSERT_EQUAL_INT16(0, after.sociability());
  TEST_ASSERT_EQUAL_UINT16(0, after.core.personality().ledger().journal(0).sequence);
}

void reset_forgets_what_xuna_became_but_keeps_its_birth() {
  Rig rig;
  rig.boot(1000);
  const TemperamentState born = rig.core.personality().genome();
  uint32_t now = rig.praiseForDays(2000, 4);
  for (uint8_t i = 0; i < 3; ++i) rig.send(8, 5, now += 1000);  // fresh evidence
  TEST_ASSERT_TRUE(rig.sociability() > 0);
  TEST_ASSERT_EQUAL_UINT8(0, rig.send(9, 2, now += 1000));
  const Personality& p = rig.core.personality();
  TEST_ASSERT_EQUAL_INT16_ARRAY(born.baseline, p.genome().baseline, kTraitCount);
  for (uint8_t t = 0; t < kTraitCount; ++t) {
    TEST_ASSERT_EQUAL_INT16(0, p.genome().offset[t]);
    TEST_ASSERT_EQUAL_INT32(0, p.ledger().trait(static_cast<TraitId>(t)).importance);
  }
  TEST_ASSERT_EQUAL_UINT16(0, p.ledger().journal(0).sequence);
  TEST_ASSERT_EQUAL_UINT8(4, rig.send(9, 1, now += 1000));  // journal is empty
  Rig after;
  after.store = rig.store;
  after.boot(now += 1000);
  TEST_ASSERT_EQUAL_INT16(0, after.sociability());
}

void bad_arguments_and_the_baseline_adapter_are_rejected() {
  Rig rig;
  rig.boot(1000);
  TEST_ASSERT_EQUAL_UINT8(4, rig.send(9, 0, 2000));
  TEST_ASSERT_EQUAL_UINT8(4, rig.send(9, 3, 2001));
  MemorySnapshotStore store;
  StoreSnapshotCommit commit(store);
  LifeCore core(42, 54);
  V1LifeCoreAdapter baseline(core, commit);
  core.begin(1000, true);
  baseline.begin(1000);
  const uint8_t bytes[8] = {1, 9, 1, 0, 0, 0, 2, 0};
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(3, baseline.handle(bytes, 8, 2000, true, id));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(personality_undo_walks_back_through_the_journal);
  RUN_TEST(undo_is_committed_first_and_survives_a_reboot);
  RUN_TEST(reset_forgets_what_xuna_became_but_keeps_its_birth);
  RUN_TEST(bad_arguments_and_the_baseline_adapter_are_rejected);
  return UNITY_END();
}
