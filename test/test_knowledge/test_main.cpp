#include <cstdio>
#include <cstring>
#include <unity.h>

#include <xuna/knowledge/KnowledgeCodec.h>
#include <xuna/knowledge/KnowledgeProfile.h>
#include <xuna/knowledge/KnowledgeProjection.h>
#include <xuna/lab/LifeScenarios.h>
#include <xuna/persist/SnapshotCodec.h>

using namespace xuna;

void setUp() {}
void tearDown() {}

// 1. Level threshold table: upper & lower bounds for both facts and days
void test_level_thresholds_table() {
  struct Row {
    uint8_t facts;
    uint8_t days;
    uint8_t expectedLevel;
  };

  const Row table[] = {
      // Below Level 1
      {0, 0, 0},
      {2, 1, 0},   // facts < 3
      {3, 0, 0},   // days < 1
      {2, 5, 0},   // facts < 3 despite high days
      // Level 1 boundary: F >= 3, D >= 1
      {3, 1, 1},   // lower bound for Level 1
      {4, 1, 1},
      {9, 1, 1},   // upper facts for Level 1 (with days 1)
      {9, 2, 1},
      {9, 3, 1},   // facts < 10 despite days >= 3
      {10, 1, 1},  // days < 3 despite facts >= 10
      {10, 2, 1},
      {25, 2, 1},  // days < 3 despite facts >= 25
      // Level 2 boundary: F >= 10, D >= 3
      {10, 3, 2},  // lower bound for Level 2
      {11, 3, 2},
      {24, 3, 2},
      {24, 4, 2},
      {24, 5, 2},  // facts < 25 despite days >= 5
      {25, 3, 2},  // days < 5 despite facts >= 25
      {25, 4, 2},
      {32, 4, 2},  // facts = max despite days < 5
      // Level 3 boundary: F >= 25, D >= 5
      {25, 5, 3},  // lower bound for Level 3
      {26, 5, 3},
      {32, 5, 3},
      {32, 10, 3},
      {32, 255, 3} // upper bound
  };

  const size_t count = sizeof(table) / sizeof(table[0]);
  for (size_t i = 0; i < count; ++i) {
    const uint8_t actual = KnowledgeProfile::computeLevel(table[i].facts, table[i].days);
    if (actual != table[i].expectedLevel) {
      char msg[64];
      snprintf(msg, sizeof(msg), "Row %u failed: facts=%u, days=%u, got=%u, exp=%u",
               static_cast<unsigned>(i), table[i].facts, table[i].days, actual,
               table[i].expectedLevel);
      TEST_FAIL_MESSAGE(msg);
    }
  }
}

// 2. Duplicate keys rejected and state unchanged
void test_duplicate_key_rejected() {
  KnowledgeProfile p;
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.open(0));
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.add(0, 0x1234, 1));
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).days);
  const uint16_t seqBefore = p.journalSeq();

  // Add duplicate key
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Duplicate, p.add(0, 0x1234, 2));
  // State must not change
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).days);
  TEST_ASSERT_EQUAL_UINT16(seqBefore, p.journalSeq());
}

// 3. Remove: existing key and non-existing key
void test_remove_key() {
  KnowledgeProfile p;
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.open(0));
  p.add(0, 0x10, 1);
  p.add(0, 0x20, 1);
  p.add(0, 0x30, 2);
  TEST_ASSERT_EQUAL_UINT8(3, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(0).days);
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).level);

  // Remove non-existing key
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::NotFound, p.remove(0, 0x99));
  TEST_ASSERT_EQUAL_UINT8(3, p.slot(0).keyCount);

  // Remove existing key (middle key 0x20)
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.remove(0, 0x20));
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(0).keyCount);
  // Days must not decrease
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(0).days);
  // Keys shifted
  TEST_ASSERT_EQUAL_UINT32(0x10, p.slot(0).keys[0]);
  TEST_ASSERT_EQUAL_UINT32(0x30, p.slot(0).keys[1]);
  // Level recomputed: 2 facts, 2 days -> Level 0
  TEST_ASSERT_EQUAL_UINT8(0, p.slot(0).level);

  // Journal entry created with reason 2 (EvidenceRemoved)
  const KnowledgeJournalEntry& j = p.journal(0);
  TEST_ASSERT_EQUAL_UINT8(0, j.slot);
  TEST_ASSERT_EQUAL_UINT8(0, j.level);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(KnowledgeReason::EvidenceRemoved), j.reason);
  TEST_ASSERT_EQUAL_UINT8(2, j.facts);
  TEST_ASSERT_EQUAL_UINT8(2, j.days);
}

// 4. Clear slot
void test_clear_slot() {
  KnowledgeProfile p;
  // Clear already empty slot
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.clear(0));
  TEST_ASSERT_EQUAL_UINT16(0, p.journalSeq());

  // Open and populate slot 0
  p.open(0);
  p.add(0, 0x1, 1);
  p.add(0, 0x2, 2);
  p.add(0, 0x3, 3);
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).level);

  // Clear populated slot
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.clear(0));
  TEST_ASSERT_EQUAL(SlotState::Empty, p.slot(0).state);
  TEST_ASSERT_EQUAL_UINT8(0, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(0, p.slot(0).days);
  TEST_ASSERT_EQUAL_UINT8(0, p.slot(0).level);

  // Journal entry created with reason 3 (Cleared)
  const KnowledgeJournalEntry& j = p.journal(0);
  TEST_ASSERT_EQUAL_UINT8(0, j.slot);
  TEST_ASSERT_EQUAL_UINT8(0, j.level);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(KnowledgeReason::Cleared), j.reason);
  TEST_ASSERT_EQUAL_UINT8(0, j.facts);
  TEST_ASSERT_EQUAL_UINT8(0, j.days);
}

// 5. Move: same slot, destination full, destination resting, not found, duplicate
void test_move_evidence() {
  KnowledgeProfile p;
  p.open(0);
  p.open(1);
  p.add(0, 0x101, 1);
  p.add(0, 0x102, 1);
  p.add(0, 0x103, 1);

  // Same slot
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Invalid, p.move(0, 0, 0x101, 1));

  // Destination resting
  p.setState(1, SlotState::Resting);
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::SlotResting, p.move(0, 1, 0x101, 1));
  p.setState(1, SlotState::Active);

  // Key not found in source
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::NotFound, p.move(0, 1, 0x999, 1));

  // Destination full (fill slot 1 with 32 keys)
  for (uint32_t k = 1; k <= 32; ++k) {
    p.add(1, 0x200 + k, 1);
  }
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Full, p.move(0, 1, 0x101, 1));

  // Clear slot 1 and test key already in destination (Duplicate)
  p.clear(1);
  p.open(1);
  p.add(1, 0x101, 1);
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Duplicate, p.move(0, 1, 0x101, 1));

  // Clean move: move 0x102 from slot 0 to slot 1
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.move(0, 1, 0x102, 2));
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(1).keyCount);

  // Journal entries: newest is slot 1 (to), age 1 is slot 0 (from)
  const KnowledgeJournalEntry& toJ = p.journal(0);
  TEST_ASSERT_EQUAL_UINT8(1, toJ.slot);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(KnowledgeReason::Moved), toJ.reason);
  TEST_ASSERT_EQUAL_UINT8(2, toJ.facts);

  const KnowledgeJournalEntry& fromJ = p.journal(1);
  TEST_ASSERT_EQUAL_UINT8(0, fromJ.slot);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(KnowledgeReason::Moved), fromJ.reason);
  TEST_ASSERT_EQUAL_UINT8(2, fromJ.facts);
}

// 6. Add to Empty or Resting slot
void test_add_to_empty_or_resting() {
  KnowledgeProfile p;
  // Slot 0 is Empty
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::SlotEmpty, p.add(0, 0x1, 1));

  p.open(0);
  p.setState(0, SlotState::Resting);
  // Slot 0 is Resting
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::SlotResting, p.add(0, 0x1, 1));
}

// 7. Trusted day vs untrusted day, and same day twice
void test_trusted_and_untrusted_days() {
  KnowledgeProfile p;
  p.open(0);

  // Untrusted day (0): fact is counted, days is not
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.add(0, 0x1, 0));
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(0, p.slot(0).days);

  // First trusted day (10)
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.add(0, 0x2, 10));
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).days);
  TEST_ASSERT_EQUAL_UINT16(10, p.slot(0).lastDay);

  // Same trusted day (10) again
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.add(0, 0x3, 10));
  TEST_ASSERT_EQUAL_UINT8(3, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(1, p.slot(0).days);

  // New trusted day (11)
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.add(0, 0x4, 11));
  TEST_ASSERT_EQUAL_UINT8(4, p.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(2, p.slot(0).days);
}

// 8. Days saturates at 255
void test_days_saturation_at_255() {
  KnowledgeProfile p;
  p.open(0);
  // Manually restore slot with days = 254
  KnowledgeSlot slots[KnowledgeProfile::kSlots];
  slots[0].state = SlotState::Active;
  slots[0].days = 254;
  slots[0].lastDay = 254;
  KnowledgeJournalEntry j[4];
  p.restore(slots, j, 0, 0);

  p.add(0, 0x1, 255);
  TEST_ASSERT_EQUAL_UINT8(255, p.slot(0).days);

  p.add(0, 0x2, 256);
  TEST_ASSERT_EQUAL_UINT8(255, p.slot(0).days);  // Saturated
}

// 9. Full when 32 keys reached
void test_slot_full_at_32_keys() {
  KnowledgeProfile p;
  p.open(0);
  for (uint32_t k = 1; k <= 32; ++k) {
    TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Ok, p.add(0, k, 1));
  }
  TEST_ASSERT_EQUAL_UINT8(32, p.slot(0).keyCount);

  // 33rd key returns Full
  TEST_ASSERT_EQUAL(KnowledgeProfile::Result::Full, p.add(0, 33, 1));
  TEST_ASSERT_EQUAL_UINT8(32, p.slot(0).keyCount);
}

// 10. Journal ring buffer wrapping and seq increment
void test_journal_wrapping_and_seq() {
  KnowledgeProfile p;
  p.open(0);

  // Perform 6 operations: seq 1..6
  for (uint32_t k = 1; k <= 6; ++k) {
    p.add(0, k, static_cast<uint16_t>(k));
    TEST_ASSERT_EQUAL_UINT16(k, p.journalSeq());
  }

  // Age 0 is newest: seq 6
  TEST_ASSERT_EQUAL_UINT16(6, p.journal(0).seq);
  TEST_ASSERT_EQUAL_UINT16(5, p.journal(1).seq);
  TEST_ASSERT_EQUAL_UINT16(4, p.journal(2).seq);
  TEST_ASSERT_EQUAL_UINT16(3, p.journal(3).seq);
}

// 11. Codec: round-trip, corrupt data, TooNew, max size
void test_codec_round_trip_and_validation() {
  uint8_t buffer[KnowledgeCodec::kMaxSize];

  // 11a: Empty profile round-trip (66 B)
  {
    KnowledgeProfile p;
    const size_t len = KnowledgeCodec::encode(p, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT32(KnowledgeCodec::kMinSize, len);
    TEST_ASSERT_TRUE(KnowledgeCodec::valid(buffer, len));

    KnowledgeProfile recovered;
    TEST_ASSERT_TRUE(KnowledgeCodec::load(buffer, len, recovered));
    TEST_ASSERT_TRUE(recovered.empty());
  }

  // 11b: Maximum profile round-trip (706 B)
  {
    KnowledgeProfile maxP;
    for (uint8_t s = 0; s < KnowledgeProfile::kSlots; ++s) {
      maxP.open(s);
      for (uint32_t k = 1; k <= 32; ++k) {
        maxP.add(s, (static_cast<uint32_t>(s) << 16) | k, static_cast<uint16_t>(k));
      }
      TEST_ASSERT_EQUAL_UINT8(3, maxP.slot(s).level);
    }
    const size_t len = KnowledgeCodec::encode(maxP, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_UINT32(KnowledgeCodec::kMaxSize, len);
    TEST_ASSERT_TRUE(KnowledgeCodec::valid(buffer, len));

    KnowledgeProfile recovered;
    TEST_ASSERT_TRUE(KnowledgeCodec::load(buffer, len, recovered));
    for (uint8_t s = 0; s < KnowledgeProfile::kSlots; ++s) {
      TEST_ASSERT_EQUAL(SlotState::Active, recovered.slot(s).state);
      TEST_ASSERT_EQUAL_UINT8(3, recovered.slot(s).level);
      TEST_ASSERT_EQUAL_UINT8(32, recovered.slot(s).keyCount);
      TEST_ASSERT_EQUAL_UINT8(32, recovered.slot(s).days);
      for (uint8_t k = 0; k < 32; ++k) {
        TEST_ASSERT_EQUAL_UINT32(maxP.slot(s).keys[k], recovered.slot(s).keys[k]);
      }
    }
    TEST_ASSERT_EQUAL_UINT16(maxP.journalSeq(), recovered.journalSeq());
  }

  // 11c: Corrupt data checks
  {
    KnowledgeProfile p;
    p.open(0);
    p.add(0, 0x10, 1);
    const size_t len = KnowledgeCodec::encode(p, buffer, sizeof(buffer));
    TEST_ASSERT_TRUE(len >= KnowledgeCodec::kMinSize);

    // Buffer too small
    TEST_ASSERT_FALSE(KnowledgeCodec::valid(buffer, len - 1));

    // Corrupted state (> 2)
    uint8_t bad[KnowledgeCodec::kMaxSize];
    std::memcpy(bad, buffer, len);
    bad[4] = 3;  // slot 0 state = 3 (invalid)
    TEST_ASSERT_FALSE(KnowledgeCodec::valid(bad, len));

    // Corrupted keyCount (> 32)
    std::memcpy(bad, buffer, len);
    bad[6] = 33;  // slot 0 keyCount = 33
    TEST_ASSERT_FALSE(KnowledgeCodec::valid(bad, len));

    // Corrupted level (stored level does not match computed level)
    std::memcpy(bad, buffer, len);
    bad[5] = 2;  // slot 0 level set to 2 when facts=1, days=1
    TEST_ASSERT_FALSE(KnowledgeCodec::valid(bad, len));
    KnowledgeProfile probe;
    TEST_ASSERT_EQUAL(int(KnowledgeCodec::DecodeResult::Corrupt),
                      int(KnowledgeCodec::decode(bad, len, probe)));

    // Duplicate key in slot
    // Construct buffer with 2 keys that are identical
    KnowledgeProfile p2;
    p2.open(0);
    p2.add(0, 0x10, 1);
    p2.add(0, 0x20, 1);
    const size_t len2 = KnowledgeCodec::encode(p2, bad, sizeof(bad));
    // Overwrite key 1 with key 0
    std::memcpy(bad + 10 + 4, bad + 10, 4);
    TEST_ASSERT_FALSE(KnowledgeCodec::valid(bad, len2));

    // Key == 0 in slot
    std::memset(bad + 10, 0, 4);
    TEST_ASSERT_FALSE(KnowledgeCodec::valid(bad, len2));

    // slotCount > 5 -> TooNew
    std::memcpy(bad, buffer, len);
    bad[0] = 6;
    TEST_ASSERT_EQUAL(int(KnowledgeCodec::DecodeResult::TooNew),
                      int(KnowledgeCodec::decode(bad, len, probe)));

    // maxKeys > 32 -> TooNew
    std::memcpy(bad, buffer, len);
    bad[1] = 64;
    TEST_ASSERT_EQUAL(int(KnowledgeCodec::DecodeResult::TooNew),
                      int(KnowledgeCodec::decode(bad, len, probe)));
  }
}

// 12. Projection byte-exact test
void test_projection_exact_bytes() {
  KnowledgeProfile p;
  p.open(0);
  p.add(0, 0x1, 1);
  p.add(0, 0x2, 2);
  p.add(0, 0x3, 3);
  // slot 0: Active, facts=3, days=3 -> level=1

  p.open(1);
  p.setState(1, SlotState::Resting);

  uint8_t out[KnowledgeProjection::kSize];
  KnowledgeProjection::encode(p, true, out);

  // Byte 0: version = 1
  TEST_ASSERT_EQUAL_UINT8(1, out[0]);
  // Byte 1: size = 64
  TEST_ASSERT_EQUAL_UINT8(64, out[1]);
  // Byte 2: flags = 1 (clockTrusted)
  TEST_ASSERT_EQUAL_UINT8(1, out[2]);
  // Byte 3: slotCount = 5
  TEST_ASSERT_EQUAL_UINT8(5, out[3]);
  // Bytes 4..5: journalSeq = 3
  TEST_ASSERT_EQUAL_UINT16(3, out[4] | (out[5] << 8));
  // Bytes 6..7: reserved = 0
  TEST_ASSERT_EQUAL_UINT16(0, out[6] | (out[7] << 8));

  // Slot 0 at offset 8: state=Active(1), level=1, facts=3, days=3, lastDay=3, reserved=0
  TEST_ASSERT_EQUAL_UINT8(1, out[8]);   // state
  TEST_ASSERT_EQUAL_UINT8(1, out[9]);   // level
  TEST_ASSERT_EQUAL_UINT8(3, out[10]);  // facts
  TEST_ASSERT_EQUAL_UINT8(3, out[11]);  // days
  TEST_ASSERT_EQUAL_UINT16(3, out[12] | (out[13] << 8));  // lastDay
  TEST_ASSERT_EQUAL_UINT16(0, out[14] | (out[15] << 8));  // reserved

  // Slot 1 at offset 16: state=Resting(2), level=0, facts=0, days=0
  TEST_ASSERT_EQUAL_UINT8(2, out[16]);
  TEST_ASSERT_EQUAL_UINT8(0, out[17]);
  TEST_ASSERT_EQUAL_UINT8(0, out[18]);

  // Slots 2..4: Empty(0)
  for (uint8_t s = 2; s < 5; ++s) {
    TEST_ASSERT_EQUAL_UINT8(0, out[8 + 8 * s]);
  }

  // Journal entries at offset 48..63: newest first
  // Entry 0 (age 0): seq=3, slot=0, (level=1 << 4 | reason=1) = 0x11
  TEST_ASSERT_EQUAL_UINT16(3, out[48] | (out[49] << 8));
  TEST_ASSERT_EQUAL_UINT8(0, out[50]);
  TEST_ASSERT_EQUAL_UINT8(0x11, out[51]);

  // Entry 1 (age 1): seq=2, slot=0, (level=0 << 4 | reason=1) = 0x01
  TEST_ASSERT_EQUAL_UINT16(2, out[52] | (out[53] << 8));
  TEST_ASSERT_EQUAL_UINT8(0, out[54]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[55]);

  // Entry 2 (age 2): seq=1, slot=0, (level=0 << 4 | reason=1) = 0x01
  TEST_ASSERT_EQUAL_UINT16(1, out[56] | (out[57] << 8));
  TEST_ASSERT_EQUAL_UINT8(0, out[58]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[59]);

  // Entry 3 (age 3): empty -> all 0
  TEST_ASSERT_EQUAL_UINT32(0, out[60] | (out[61] << 8) | (out[62] << 16) | (out[63] << 24));

  // Test clockTrusted = false sets flag = 0
  KnowledgeProjection::encode(p, false, out);
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);
}

// 13. Snapshot with and without section 13
void test_snapshot_section_13_integration() {
  uint8_t rawSnapshot[SnapshotCodec::kBudget];

  // 13a: Snapshot with section 13
  {
    LifeSnapshotV2 snap;
    snap.generation = 42;
    snap.counters.bootCounter = 1;
    snap.counters.activeSeconds = 120;

    KnowledgeProfile original;
    original.open(0);
    original.add(0, 0xABC, 1);
    original.add(0, 0xDEF, 2);
    original.add(0, 0x123, 3);

    snap.knowledge.length =
        static_cast<uint16_t>(KnowledgeCodec::encode(original, snap.knowledge.bytes,
                                                     sizeof(snap.knowledge.bytes)));
    TEST_ASSERT_TRUE(snap.knowledge.length > 0);

    const size_t snapSize = SnapshotCodec::encode(snap, rawSnapshot, sizeof(rawSnapshot));
    TEST_ASSERT_TRUE(snapSize > 0);
    TEST_ASSERT_TRUE(snapSize <= SnapshotCodec::kBudget);

    LifeSnapshotV2 recovered;
    const SnapshotDecodeInfo info = SnapshotCodec::decode(rawSnapshot, snapSize, recovered);
    TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
    TEST_ASSERT_EQUAL_UINT16(snap.knowledge.length, recovered.knowledge.length);

    KnowledgeProfile recoveredProfile;
    TEST_ASSERT_TRUE(KnowledgeCodec::load(recovered.knowledge.bytes,
                                          recovered.knowledge.length, recoveredProfile));
    TEST_ASSERT_EQUAL_UINT8(original.slot(0).level, recoveredProfile.slot(0).level);
    TEST_ASSERT_EQUAL_UINT8(original.slot(0).keyCount, recoveredProfile.slot(0).keyCount);
    TEST_ASSERT_EQUAL_UINT8(original.slot(0).days, recoveredProfile.slot(0).days);
    TEST_ASSERT_EQUAL_UINT16(original.journalSeq(), recoveredProfile.journalSeq());
  }

  // 13b: Snapshot without section 13
  {
    LifeSnapshotV2 snap;
    snap.generation = 43;
    snap.counters.bootCounter = 1;
    snap.knowledge.length = 0;  // No section 13

    const size_t snapSize = SnapshotCodec::encode(snap, rawSnapshot, sizeof(rawSnapshot));
    TEST_ASSERT_TRUE(snapSize > 0);

    LifeSnapshotV2 recovered;
    const SnapshotDecodeInfo info = SnapshotCodec::decode(rawSnapshot, snapSize, recovered);
    TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
    TEST_ASSERT_EQUAL_UINT16(0, recovered.knowledge.length);

    KnowledgeProfile emptyProfile;
    // Without section 13, empty profile remains empty
    TEST_ASSERT_TRUE(emptyProfile.empty());
  }
}

// 14. Life Lab: two teaching histories producing different profiles & explainable journals
void test_life_lab_two_teaching_histories() {
  const LifeScenarios::KnowledgeDualReport r = LifeScenarios::knowledgeTwoHistories();
  TEST_ASSERT_TRUE(r.passed);

  // Profile A (Steady: 12 facts across 4 days) -> Level 2
  TEST_ASSERT_EQUAL_UINT8(2, r.steady.slot(0).level);
  TEST_ASSERT_EQUAL_UINT8(12, r.steady.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(4, r.steady.slot(0).days);

  // Profile B (Cram: 25 facts in 1 day) -> Level 1
  TEST_ASSERT_EQUAL_UINT8(1, r.cram.slot(0).level);
  TEST_ASSERT_EQUAL_UINT8(25, r.cram.slot(0).keyCount);
  TEST_ASSERT_EQUAL_UINT8(1, r.cram.slot(0).days);

  // Clear differences asserted:
  // Cram has more facts (25 > 12), but lower level (1 < 2) due to only 1 teaching day
  TEST_ASSERT_TRUE(r.cram.slot(0).keyCount > r.steady.slot(0).keyCount);
  TEST_ASSERT_TRUE(r.steady.slot(0).level > r.cram.slot(0).level);

  // Journal explains level changes:
  // Steady reached level 2: newest journal entry has level = 2, facts = 12, days = 4
  const KnowledgeJournalEntry& steadyJ = r.steady.journal(0);
  TEST_ASSERT_EQUAL_UINT8(2, steadyJ.level);
  TEST_ASSERT_EQUAL_UINT8(12, steadyJ.facts);
  TEST_ASSERT_EQUAL_UINT8(4, steadyJ.days);

  // Cram newest journal entry has level = 1, facts = 25, days = 1 (remained at level 1)
  const KnowledgeJournalEntry& cramJ = r.cram.journal(0);
  TEST_ASSERT_EQUAL_UINT8(1, cramJ.level);
  TEST_ASSERT_EQUAL_UINT8(25, cramJ.facts);
  TEST_ASSERT_EQUAL_UINT8(1, cramJ.days);
}

// 15. Measure snapshot budget
void test_measure_snapshot_budget() {
  printf("\n=== SNAPSHOT BUDGET MEASUREMENT (KD-1) ===\n");
  printf("KnowledgeCodec::kMaxSize: %u bytes\n", static_cast<unsigned>(KnowledgeCodec::kMaxSize));
  printf("SnapshotCodec::kBudget:   %u bytes\n", static_cast<unsigned>(SnapshotCodec::kBudget));

  // Calculate snapshot size with various combinations
  LifeSnapshotV2 snap(productLearnerConfig());
  snap.counters.bootCounter = 1;
  snap.counters.activeSeconds = 1000;
  snap.clock.lastTrustedUtcMinute = 12345;
  snap.hasNeeds = true;
  snap.needs.energy = 30000;
  snap.needs.stimulation = 30000;

  // 1. Snapshot with typical fresh pet + full knowledge:
  snap.knowledge.length = KnowledgeCodec::kMaxSize;  // 706 B
  const size_t sizeFreshWithFullKnowledge = SnapshotCodec::encodedSize(snap);
  printf("Fresh snapshot + full knowledge (706 B): %u bytes\n",
         static_cast<unsigned>(sizeFreshWithFullKnowledge));

  // 2. Worst-case theoretical snapshot with ALL optional sections at max:
  // - Habit section:
  for (uint8_t i = 0; i < HabitState::kSlots; ++i) {
    snap.habit.counts[i] = 10;
    snap.habit.minutes[i] = 30;
  }
  snap.habit.sessions = 5;
  // - Personality section (PE-3 max 482 B):
  snap.personality.length = PersonalityCodec::kMaxSize;
  // - Stated preference (S24a):
  // (learner with stated mass)
  const size_t sizeWorstCaseTheoretical = SnapshotCodec::encodedSize(snap);
  printf("Worst-case theoretical snapshot (all sections max + 706 B knowledge): %u bytes\n",
         static_cast<unsigned>(sizeWorstCaseTheoretical));
  printf("Budget status: %s (difference: %+d bytes vs %u)\n",
         sizeWorstCaseTheoretical <= SnapshotCodec::kBudget ? "WITHIN BUDGET" : "EXCEEDS BUDGET",
         static_cast<int>(sizeWorstCaseTheoretical) - static_cast<int>(SnapshotCodec::kBudget),
         static_cast<unsigned>(SnapshotCodec::kBudget));
  printf("==========================================\n");

  TEST_ASSERT_EQUAL_UINT32(706, KnowledgeCodec::kMaxSize);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_level_thresholds_table);
  RUN_TEST(test_duplicate_key_rejected);
  RUN_TEST(test_remove_key);
  RUN_TEST(test_clear_slot);
  RUN_TEST(test_move_evidence);
  RUN_TEST(test_add_to_empty_or_resting);
  RUN_TEST(test_trusted_and_untrusted_days);
  RUN_TEST(test_days_saturation_at_255);
  RUN_TEST(test_slot_full_at_32_keys);
  RUN_TEST(test_journal_wrapping_and_seq);
  RUN_TEST(test_codec_round_trip_and_validation);
  RUN_TEST(test_projection_exact_bytes);
  RUN_TEST(test_snapshot_section_13_integration);
  RUN_TEST(test_life_lab_two_teaching_histories);
  RUN_TEST(test_measure_snapshot_budget);
  return UNITY_END();
}
