#include <unity.h>

#include <cstring>

#include <xuna/life/LifeCoreSnapshotLab.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>

using namespace xuna;

namespace {

// S08a: observed BLE v1 bytes from the owner's board (S01, 2026-10-13,
// docs/roadmap/S01-v1-observed-transcript.md), firmware c84d4e3.
size_t hex(const char* text, uint8_t* out) {
  size_t n = 0;
  int high = -1;
  for (const char* p = text; *p; ++p) {
    int v = -1;
    if (*p >= '0' && *p <= '9') v = *p - '0';
    if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
    if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
    if (v < 0) continue;
    if (high < 0) {
      high = v;
    } else {
      out[n++] = static_cast<uint8_t>(high << 4 | v);
      high = -1;
    }
  }
  return n;
}

struct Rig {
  MemorySnapshotStore store;
  StoreSnapshotCommit commit;
  LifeCore core;
  V1LifeCoreAdapter adapter;
  explicit Rig(bool greeting = false, bool enhanced = false)
      : commit(store), core(42, 54), adapter(core, commit, greeting, enhanced) {}

  // Boot exactly like the S13 adapter: recover (v1 → v2 migration), restore,
  // begin. Returns the recovery result.
  LoadResult boot(uint32_t now) {
    LifeSnapshotV2 snapshot;
    const RecoveryReport report = SnapshotStore::recover(store, snapshot);
    if (report.result == LoadResult::Ok ||
        report.result == LoadResult::Migrated)
      core.restore(snapshot);
    core.begin(now, true);
    adapter.begin(now);
    return report.result;
  }

  uint8_t send(const char* text, uint32_t now, uint32_t& id) {
    uint8_t bytes[16];
    const size_t n = hex(text, bytes);
    return adapter.handle(bytes, n, now, true, id);
  }
};

// The S01 baseline pet: one confirmed-text approval of quiet/Idle,
// activeSeconds 4009, written to both v1 keys like LifeStorage would.
void putS01Pet(MemorySnapshotStore& store) {
  LifeEngine life;
  life.activeSeconds = 4009;
  life.feedback(0, 0, true, 2);
  uint8_t bytes[LifeEngine::kSnapshotSize];
  life.generation = 1;
  life.encode(bytes);
  store.put(SnapshotKey::V1Slot1, bytes, sizeof(bytes));
}

// Compares a 20-byte value, ignoring activeSeconds (bytes 4–7, wall time on
// the board) and, when `anyAction`, the shown action byte 3.
void assertLife(const char* expected, const V1LifeCoreAdapter& adapter,
                bool anyAction = false) {
  uint8_t want[20];
  uint8_t got[20];
  TEST_ASSERT_EQUAL_UINT32(20, hex(expected, want));
  adapter.encodeLife(got);
  for (uint8_t i = 0; i < 20; ++i) {
    if (i >= 4 && i <= 7) continue;
    if (anyAction && i == 3) continue;
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(want[i], got[i], "life byte");
  }
}

void assertMemory(uint8_t page, const char* expected,
                  const V1LifeCoreAdapter& adapter) {
  uint8_t want[20];
  uint8_t got[20];
  TEST_ASSERT_EQUAL_UINT32(20, hex(expected, want));
  adapter.encodeMemory(page, got);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 20);
}

// Status bytes 0–6 (version, kind, request ID, result). Byte 7 is the
// renderer state, 8–15 the board's sequence and uptime.
void assertStatus(const char* expected, uint32_t id, uint8_t result) {
  uint8_t want[16];
  uint8_t got[16];
  TEST_ASSERT_EQUAL_UINT32(16, hex(expected, want));
  V1LifeCoreAdapter::encodeStatus(1, id, result, 0, 0, 0, got);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 7);
}

bool isLearnAction(uint8_t action) {
  return action == uint8_t(ActionId::Idle) ||
         action == uint8_t(ActionId::Happy) ||
         action == uint8_t(ActionId::Curious);
}

const uint32_t kGap = 10000;  // compressed: well inside D3 and cooldowns

}  // namespace

// The full S01 inspector script, replayed through LifeCore with compressed
// timing. Every result code, counter, decision, feedback flag and memory byte
// must equal the observed v1 bytes; only the shown action may differ (v3
// UCB may explore), and then it must be reported consistently.
void s01_transcript_replays_through_lifecore() {
  Rig rig;
  putS01Pet(rig.store);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Migrated),
                          uint8_t(rig.boot(1000)));
  V1LifeCoreAdapter& a = rig.adapter;
  uint32_t t = 1000;
  uint32_t id = 0;

  assertLife("01 14 00 00 A9 0F 00 00 01 00 00 00 00 00 00 00 00 00 00 01", a);
  assertMemory(0, "01 01 00 00 00 00 01 02 00 00 00 00 00 00 00 00 00 00 00 00", a);
  assertMemory(1, "01 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", a);
  uint8_t life[20];
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(4009, LifeEngine::get32(life + 4));

  // Invalid length (7 bytes).
  uint8_t r = rig.send("01 02 01 00 00 00 00", t += kGap, id);
  assertStatus("01 01 01 00 00 00 02 00 03 00 00 00 20 F0 27 00", id, r);
  assertLife("01 14 00 00 B9 14 00 00 01 00 00 00 00 00 00 00 00 00 00 01", a);

  // Valid quiet suggestion, request 2.
  r = rig.send("01 02 02 00 00 00 00 00", t += kGap, id);
  assertStatus("01 01 02 00 00 00 00 00 03 00 00 00 3E EF 29 00", id, r);
  assertLife("01 14 00 00 35 15 00 00 01 00 00 00 02 00 00 00 00 00 01 01", a,
             true);
  a.encodeLife(life);
  const uint8_t shown = life[3];
  TEST_ASSERT_TRUE(isLearnAction(shown));

  // Approval request 3 → target 2, button source.
  r = rig.send("01 03 03 00 00 00 01 01 02 00 00 00", t += kGap, id);
  assertStatus("01 01 03 00 00 00 00 00 03 00 00 00 6C 80 2C 00", id, r);
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(2, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_UINT32(2, LifeEngine::get32(life + 12));
  TEST_ASSERT_EQUAL_UINT8(0, life[18]);
  TEST_ASSERT_EQUAL_UINT8(2, life[19]);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(a.preferred(0)), life[16]);
  if (shown == uint8_t(ActionId::Idle)) {
    assertLife("01 14 00 00 D3 15 00 00 02 00 00 00 02 00 00 00 00 00 00 02", a);
    assertMemory(0, "01 02 00 00 00 00 01 02 00 00 01 01 00 00 00 00 00 00 00 00", a);
  } else {
    uint8_t page[20];
    a.encodeMemory(0, page);
    TEST_ASSERT_EQUAL_UINT8(2, page[1]);
    TEST_ASSERT_EQUAL_UINT8(shown, page[9]);
    TEST_ASSERT_EQUAL_UINT8(1, page[10]);
    TEST_ASSERT_EQUAL_UINT8(1, page[11]);
  }

  // Exact duplicate: cached OK, no second learning.
  uint8_t before[20];
  a.encodeLife(before);
  r = rig.send("01 03 03 00 00 00 01 01 02 00 00 00", t += kGap, id);
  assertStatus("01 01 03 00 00 00 00 00 03 00 00 00 AC ED 2D 00", id, r);
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(2, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(before + 8, life + 8, 12);

  // Reused ID 3 with another payload: request_id_conflict.
  r = rig.send("010303000000000102000000", t += kGap, id);
  assertStatus("01 01 03 00 00 00 06 00 03 00 00 00 6C E1 2E 00", id, r);
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(before + 8, life + 8, 12);

  // Feedback without a pending decision.
  r = rig.send("010304000000010102000000", t += kGap, id);
  assertStatus("01 01 04 00 00 00 04 00 03 00 00 00 92 99 2F 00", id, r);
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(before + 8, life + 8, 12);

  // Suggestion 5, then the owner presses GPIO0.
  r = rig.send("0102050000000000", t += kGap, id);
  assertStatus("01 01 05 00 00 00 00 00 03 00 00 00 15 8E 30 00", id, r);
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(5, LifeEngine::get32(life + 12));
  TEST_ASSERT_EQUAL_UINT8(1, life[18]);
  // Pressed after the 10 s reaction window (D6 revised 2026-10-19), as on the
  // board: a direct request, not a pet.
  TEST_ASSERT_TRUE(a.localGreeting(t += kGap + 1000));

  // Feedback for 5 after the button: invalid_argument, nothing learned.
  r = rig.send("010306000000010105000000", t += kGap, id);
  assertStatus("01 01 06 00 00 00 04 00 05 00 00 00 9D 09 32 00", id, r);
  a.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(2, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_UINT32(0, LifeEngine::get32(life + 12));
  TEST_ASSERT_EQUAL_UINT8(0, life[18]);
  TEST_ASSERT_EQUAL_UINT8(2, life[19]);

  // Forget: counters and memories cleared, lifetime kept.
  r = rig.send("0104070000000000", t += kGap, id);
  assertStatus("01 01 07 00 00 00 00 00 05 00 00 00 0B C8 32 00", id, r);
  assertLife("01 14 00 00 67 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00", a,
             true);
  assertMemory(0, "01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", a);
  assertMemory(1, "01 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00", a);
  a.encodeLife(life);
  TEST_ASSERT_TRUE(LifeEngine::get32(life + 4) >= 4009);
}

// The same approval with the board's real uptimes (status bytes 12–15):
// the inspector needed 168 s between suggestion and rating, beyond the D3
// window. The rating is rejected (4) and teaches nothing.
void s01_real_timing_approval_is_expired_by_d3() {
  Rig rig;
  putS01Pet(rig.store);
  rig.boot(124133);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 00 00", 2748222, id));
  TEST_ASSERT_EQUAL_UINT8(
      4, rig.send("01 03 03 00 00 00 01 01 02 00 00 00", 2916460, id));
  uint8_t life[20];
  rig.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(1, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_UINT8(0, life[18]);
  TEST_ASSERT_EQUAL_UINT8(1, life[19]);
}

// §9.5 through the adapter: a failed commit reports not_ready (7), keeps
// RAM unchanged and the decision rateable; a retry with a new ID applies.
void failed_commit_reports_not_ready_and_keeps_the_decision() {
  Rig rig;
  putS01Pet(rig.store);
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 00 00", 2000, id));
  rig.store.powerCutNextWrite();
  TEST_ASSERT_EQUAL_UINT8(
      7, rig.send("01 03 03 00 00 00 01 01 02 00 00 00", 3000, id));
  uint8_t life[20];
  rig.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(1, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_UINT8(1, life[18]);
  TEST_ASSERT_EQUAL_UINT8(
      0, rig.send("01 03 04 00 00 00 01 01 02 00 00 00", 4000, id));
  rig.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(2, LifeEngine::get32(life + 8));
}

// D2a: v2 lives in life0/life1; the v1 keys stay byte-identical (rollback).
// After a reboot the newest v2 slot is used, not a second migration.
void v1_keys_stay_untouched_and_v2_survives_reboot() {
  Rig rig;
  putS01Pet(rig.store);
  uint8_t v1Before[LifeEngine::kSnapshotSize];
  size_t size = 0;
  TEST_ASSERT_TRUE(rig.store.read(SnapshotKey::V1Slot1, v1Before,
                                  sizeof(v1Before), size));
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 00 00", 2000, id));
  TEST_ASSERT_EQUAL_UINT8(
      0, rig.send("01 03 03 00 00 00 00 01 02 00 00 00", 3000, id));
  const ActionId quiet = rig.adapter.preferred(0);

  uint8_t v1After[LifeEngine::kSnapshotSize];
  TEST_ASSERT_TRUE(rig.store.read(SnapshotKey::V1Slot1, v1After,
                                  sizeof(v1After), size));
  TEST_ASSERT_EQUAL_HEX8_ARRAY(v1Before, v1After, sizeof(v1Before));
  TEST_ASSERT_FALSE(rig.store.exists(SnapshotKey::V1Slot0));

  Rig rebooted;
  rebooted.store = rig.store;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok),
                          uint8_t(rebooted.boot(500)));
  uint8_t life[20];
  rebooted.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(2, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_UINT8(2, life[19]);
  TEST_ASSERT_EQUAL_UINT8(0, life[18]);  // pending decisions never survive
  TEST_ASSERT_EQUAL_UINT8(uint8_t(quiet), uint8_t(rebooted.adapter.preferred(0)));
}

// Active lifetime accumulates every second and reaches storage at the
// v1 five-minute checkpoint.
void active_lifetime_checkpoints_every_five_minutes() {
  Rig rig;
  putS01Pet(rig.store);
  rig.boot(0);
  rig.adapter.tick(150000);
  TEST_ASSERT_FALSE(rig.adapter.checkpointDue(150000));
  rig.adapter.tick(300500);
  TEST_ASSERT_TRUE(rig.adapter.checkpointDue(300500));
  TEST_ASSERT_TRUE(rig.adapter.checkpoint(300500));
  TEST_ASSERT_FALSE(rig.adapter.checkpointDue(300600));

  Rig rebooted;
  rebooted.store = rig.store;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok), uint8_t(rebooted.boot(0)));
  uint8_t life[20];
  rebooted.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(4009 + 300, LifeEngine::get32(life + 4));
}

// Learnable greeting (D5) must never leak context 2 into v1 bytes: the
// current app rejects unknown context codes.
void greeting_learning_keeps_v1_bytes_parseable() {
  Rig rig(true);
  putS01Pet(rig.store);
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 00 00", 2000, id));
  // Outside the reaction window, so the press is a new greeting.
  TEST_ASSERT_TRUE(rig.adapter.localGreeting(13000));
  uint8_t life[20];
  rig.adapter.encodeLife(life);
  TEST_ASSERT_TRUE(life[2] <= 1);
  TEST_ASSERT_EQUAL_UINT32(0, LifeEngine::get32(life + 12));
  TEST_ASSERT_EQUAL_UINT8(0, life[18]);
  TEST_ASSERT_TRUE(rig.adapter.last().hasDecision);  // greeting is pending
}

// D6 revised (owner 2026-10-19): the owner's reaction on the device right
// after XUNA acts teaches it, weighted 0.3 and recorded with source 3.
uint32_t positiveMass(const Rig& rig, uint8_t interaction, uint8_t action) {
  return rig.core.learner().view(interaction, 0, action).positiveQ16;
}
uint32_t negativeMass(const Rig& rig, uint8_t interaction, uint8_t action) {
  return rig.core.learner().view(interaction, 0, action).negativeQ16;
}

void pet_right_after_a_reaction_teaches_with_reaction_weight() {
  Rig rig(true, true);
  putS01Pet(rig.store);
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 01 00", 2000, id));
  uint8_t shown = 0;
  TEST_ASSERT_TRUE(learnIndexFor(
      static_cast<ActionId>(rig.adapter.last().selectedAction), shown));
  uint8_t life[20];
  rig.adapter.encodeLife(life);
  const uint32_t count = LifeEngine::get32(life + 8);
  const uint32_t before = positiveMass(rig, 1, shown);

  TEST_ASSERT_TRUE(rig.adapter.localGreeting(6000));
  TEST_ASSERT_TRUE(rig.adapter.lastReactionApplied());
  TEST_ASSERT_TRUE(rig.adapter.last().reactionApplied);
  rig.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(count + 1, LifeEngine::get32(life + 8));
  TEST_ASSERT_EQUAL_UINT8(0, life[18]);  // nothing left to rate
  // Discounted old mass plus w_r = 0.3 (19661 in Q16).
  const uint32_t after = positiveMass(rig, 1, shown);
  TEST_ASSERT_TRUE(after > before);
  TEST_ASSERT_TRUE(after - before <= 19661U);
  const SnapshotEpisodes& e = rig.core.snapshot().episodes;
  const uint8_t latest = uint8_t((e.next + SnapshotEpisodes::kCapacity - 1) %
                                 SnapshotEpisodes::kCapacity);
  TEST_ASSERT_EQUAL_UINT8(ProductLearner::kSourceReaction, e.items[latest].source);
  TEST_ASSERT_EQUAL_UINT8(1, e.items[latest].approved);

  // A second press is a new greeting, not a second pet.
  TEST_ASSERT_TRUE(rig.adapter.localGreeting(7000));
  TEST_ASSERT_FALSE(rig.adapter.lastReactionApplied());
  rig.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(count + 1, LifeEngine::get32(life + 8));
}

void shoo_right_after_a_reaction_teaches_negative_then_idles() {
  Rig rig(true, true);
  putS01Pet(rig.store);
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 01 00", 2000, id));
  uint8_t shown = 0;
  TEST_ASSERT_TRUE(learnIndexFor(
      static_cast<ActionId>(rig.adapter.last().selectedAction), shown));
  const uint32_t before = negativeMass(rig, 1, shown);
  rig.adapter.localIdle(5000);
  TEST_ASSERT_TRUE(rig.adapter.lastReactionApplied());
  TEST_ASSERT_TRUE(negativeMass(rig, 1, shown) > before);
  TEST_ASSERT_FALSE(rig.adapter.last().hasDecision);
}

void press_after_the_reaction_window_teaches_nothing() {
  Rig rig(true, true);
  putS01Pet(rig.store);
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(0, rig.send("01 02 02 00 00 00 01 00", 2000, id));
  const uint32_t count = rig.core.snapshot().counters.feedbackCount;
  rig.adapter.tick(12001);
  rig.adapter.localIdle(12001);
  TEST_ASSERT_FALSE(rig.adapter.lastReactionApplied());
  TEST_ASSERT_EQUAL_UINT32(count, rig.core.snapshot().counters.feedbackCount);
}

void spontaneous_choice_can_be_petted() {
  Rig rig(true, true);
  putS01Pet(rig.store);
  rig.boot(1000);
  TEST_ASSERT_TRUE(rig.adapter.localSpontaneous(70000));
  TEST_ASSERT_EQUAL_UINT8(0, rig.adapter.last().decisionContext);  // quiet
  uint8_t life[20];
  rig.adapter.encodeLife(life);
  TEST_ASSERT_EQUAL_UINT32(0, LifeEngine::get32(life + 12));  // not app-rateable
  const uint32_t count = rig.core.snapshot().counters.feedbackCount;
  TEST_ASSERT_TRUE(rig.adapter.localGreeting(73000));
  TEST_ASSERT_TRUE(rig.adapter.lastReactionApplied());
  TEST_ASSERT_EQUAL_UINT32(count + 1, rig.core.snapshot().counters.feedbackCount);
}

void reserved_decision_ids_and_unsupported_opcodes_are_rejected() {
  Rig rig;
  putS01Pet(rig.store);
  rig.boot(1000);
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_UINT8(4, rig.send("01 02 00 00 00 80 00 00", 2000, id));
  // Opcode 6 stays unknown on this path until S26 enables bit 5.
  TEST_ASSERT_EQUAL_UINT8(
      3, rig.send("01 06 09 00 00 00 01 02 00 00 00 00", 3000, id));
  // A storage fault reports not_ready before any dispatch.
  uint8_t bytes[8] = {1, 1, 10, 0, 0, 0, 1, 0};
  TEST_ASSERT_EQUAL_UINT8(7, rig.adapter.handle(bytes, 8, 4000, false, id));
}

// Safe recovery must remain read-only even if a caller attempts a checkpoint.
void error_safe_never_commits() {
  Rig rig;
  rig.core.begin(0, false);
  rig.adapter.begin(0);
  rig.adapter.tick(300000);
  TEST_ASSERT_FALSE(rig.adapter.checkpoint(300000));
  TEST_ASSERT_FALSE(rig.adapter.localIdle(300001));
  TEST_ASSERT_FALSE(rig.adapter.localGreeting(300002));
  TEST_ASSERT_FALSE(rig.core.forget());
  TEST_ASSERT_FALSE(rig.store.exists(SnapshotKey::V2Slot0));
  TEST_ASSERT_FALSE(rig.store.exists(SnapshotKey::V2Slot1));
}

void enhanced_statements_are_committed_explained_and_deduplicated() {
  Rig rig(false,true);rig.boot(1000);uint32_t id=0;uint8_t bytes[20];
  rig.adapter.encodeExplanation(bytes);TEST_ASSERT_EQUAL_UINT8(0,bytes[5]);
  TEST_ASSERT_EQUAL_UINT8(0,rig.send("01 06 0a 00 00 00 01 02 00 01 00 00",2000,id));
  rig.adapter.encodeExplanation(bytes);
  TEST_ASSERT_EQUAL_UINT8(4,bytes[5]); // Told
  TEST_ASSERT_EQUAL_UINT8(1,bytes[4]); // learner Happy
  TEST_ASSERT_EQUAL_UINT8(128,bytes[16]); // stated +0.5 Q8.8
  TEST_ASSERT_EQUAL_UINT32(0,rig.core.snapshot().counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(0,rig.send("01 06 0a 00 00 00 01 02 00 01 00 00",3000,id));
  rig.adapter.encodeExplanation(bytes);TEST_ASSERT_EQUAL_UINT8(128,bytes[16]);
  Rig rebooted(false,true);rebooted.store=rig.store;rebooted.boot(0);
  TEST_ASSERT_EQUAL_UINT8(unsigned(ActionId::Happy),unsigned(rebooted.adapter.preferred(0)));
  rig.adapter.onSession();rig.adapter.encodeExplanation(bytes);TEST_ASSERT_EQUAL_UINT8(0,bytes[5]);
}
void enhanced_expiry_after_tick_has_a_dedicated_result() {
  Rig rig(false,true);rig.boot(0);uint32_t id=0;
  TEST_ASSERT_EQUAL_UINT8(0,rig.send("01 02 01 00 00 00 00 00",1000,id));
  rig.adapter.tick(122000);
  TEST_ASSERT_EQUAL_UINT8(8,rig.send("01 03 02 00 00 00 01 01 01 00 00 00",123000,id));
  TEST_ASSERT_EQUAL_UINT32(0,rig.core.snapshot().counters.feedbackCount);
}

void enhanced_explanation_is_commit_grounded_and_cleared_by_direct_input() {
  Rig rig(false,true);rig.boot(0);uint32_t id=0;uint8_t before[20],after[20];
  TEST_ASSERT_EQUAL_UINT8(0,rig.send("01 06 01 00 00 00 01 02 00 01 00 00",1000,id));
  rig.adapter.encodeExplanation(before);rig.store.powerCutNextWrite();
  TEST_ASSERT_EQUAL_UINT8(7,rig.send("01 06 02 00 00 00 01 02 00 03 00 00",2000,id));
  rig.adapter.encodeExplanation(after);TEST_ASSERT_EQUAL_HEX8_ARRAY(before,after,20);
  TEST_ASSERT_EQUAL_UINT8(4,rig.send("01 06 03 00 00 00 01 02 00 02 00 00",3000,id)); // Sleep not learnable
  TEST_ASSERT_EQUAL_UINT8(4,rig.send("01 06 04 00 00 00 01 03 00 01 00 00",4000,id)); // no voice
  TEST_ASSERT_EQUAL_UINT8(0,rig.send("01 02 05 00 00 00 00 00",5000,id));
  TEST_ASSERT_EQUAL_UINT8(0,rig.send("01 03 06 00 00 00 01 01 05 00 00 00",6000,id));
  rig.adapter.encodeExplanation(after);TEST_ASSERT_EQUAL_UINT8(1,after[7]);
  TEST_ASSERT_TRUE(rig.adapter.localIdle(15000));
  rig.adapter.encodeExplanation(after);TEST_ASSERT_EQUAL_UINT8(0,after[5]);
}

void time_sync_accepts_frozen_frame_and_rejects_rollback() {
  Rig rig(false, true);rig.boot(1000);uint32_t id=0;
  TEST_ASSERT_EQUAL_UINT8(
      0,rig.send("01 05 78 56 34 12 01 00 00 f1 53 65 a4 01 00 00",2000,id));
  TEST_ASSERT_EQUAL_UINT32(0x12345678,id);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ClockQuality::Synced),
                          uint8_t(rig.adapter.clock().quality(rig.adapter.monoNow())));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(DayPart::Morning),
                          uint8_t(rig.adapter.clock().dayPart(rig.adapter.monoNow())));
  TEST_ASSERT_EQUAL_INT16(420,rig.adapter.clock().tzOffsetMin());
  // Six minutes behind the accepted watermark exceeds the frozen tolerance.
  TEST_ASSERT_EQUAL_UINT8(
      4,rig.send("01 05 79 56 34 12 01 00 98 ef 53 65 a4 01 00 00",3000,id));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(time_sync_accepts_frozen_frame_and_rejects_rollback);
RUN_TEST(pet_right_after_a_reaction_teaches_with_reaction_weight);
RUN_TEST(shoo_right_after_a_reaction_teaches_negative_then_idles);
RUN_TEST(press_after_the_reaction_window_teaches_nothing);
RUN_TEST(spontaneous_choice_can_be_petted);
  RUN_TEST(enhanced_explanation_is_commit_grounded_and_cleared_by_direct_input);
  RUN_TEST(enhanced_statements_are_committed_explained_and_deduplicated);
  RUN_TEST(enhanced_expiry_after_tick_has_a_dedicated_result);
  RUN_TEST(error_safe_never_commits);
  RUN_TEST(s01_transcript_replays_through_lifecore);
  RUN_TEST(s01_real_timing_approval_is_expired_by_d3);
  RUN_TEST(failed_commit_reports_not_ready_and_keeps_the_decision);
  RUN_TEST(v1_keys_stay_untouched_and_v2_survives_reboot);
  RUN_TEST(active_lifetime_checkpoints_every_five_minutes);
  RUN_TEST(greeting_learning_keeps_v1_bytes_parseable);
  RUN_TEST(reserved_decision_ids_and_unsupported_opcodes_are_rejected);
  return UNITY_END();
}
