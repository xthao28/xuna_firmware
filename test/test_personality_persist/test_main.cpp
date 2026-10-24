#include <cstring>

#include <unity.h>

#include <xuna/base/Crc32.h>
#include <xuna/lab/BoardLab.h>
#include <xuna/persist/SnapshotCodec.h>
#include <xuna/personality/PersonalityCodec.h>
#include <xuna/personality/PersonalityProjection.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

// Personality PE-3: snapshot section 5, reboot survival, BLE projection.

namespace {

const uint32_t kMinute = 60000;
const uint32_t kSeed = 0x5eed1234U;

// One owner session at `start`: four pets of a curious choice (p̂ 0.5), then
// the owner leaves and the session closes after the 30-minute gap.
uint32_t petSession(Personality& p, uint32_t start) {
  for (uint32_t i = 0; i < 4; ++i) {
    p.shown(2, 0, false, 32768);
    p.ownerInput(start + i * 1000);
    p.reaction(true);
  }
  p.step(start + 31 * kMinute, Activity::Idle, 65535);
  return start + 40 * kMinute;
}

// Curiosity moved once (4 sessions), then two more sessions pending.
Personality learned() {
  Personality p(kSeed);
  p.begin(0);
  uint32_t t = kMinute;
  for (int i = 0; i < 6; ++i) t = petSession(p, t);
  return p;
}

void sameState(const Personality& a, const Personality& b) {
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.genome().baseline, b.genome().baseline, kTraitCount);
  TEST_ASSERT_EQUAL_INT16_ARRAY(a.genome().offset, b.genome().offset, kTraitCount);
  TEST_ASSERT_EQUAL_UINT32(a.sessions(), b.sessions());
  TEST_ASSERT_EQUAL_UINT32(a.evidence(), b.evidence());
  TEST_ASSERT_EQUAL_UINT32(a.shifts(), b.shifts());
  TEST_ASSERT_EQUAL_UINT32(a.decays(), b.decays());
  for (uint8_t t = 0; t < kTraitCount; ++t) {
    const TraitLedger& x = a.ledger().trait(static_cast<TraitId>(t));
    const TraitLedger& y = b.ledger().trait(static_cast<TraitId>(t));
    TEST_ASSERT_EQUAL_INT32(x.pending, y.pending);
    TEST_ASSERT_EQUAL_INT32(x.importance, y.importance);
    TEST_ASSERT_EQUAL_UINT32(x.lastSession, y.lastSession);
    TEST_ASSERT_EQUAL_INT32(x.sessionNet, y.sessionNet);
    TEST_ASSERT_EQUAL_UINT8(x.sessions, y.sessions);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(x.reasonWeight, y.reasonWeight, kEvidenceReasonCount);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(x.sourceWeight, y.sourceWeight, kEvidenceSourceCount);
  }
  for (uint8_t i = 0; i < EvidenceLedger::kJournalSize; ++i) {
    TEST_ASSERT_EQUAL_UINT16(a.ledger().journalSlot(i).sequence, b.ledger().journalSlot(i).sequence);
    TEST_ASSERT_EQUAL_INT16(a.ledger().journalSlot(i).delta, b.ledger().journalSlot(i).delta);
  }
  TEST_ASSERT_EQUAL_UINT8(a.ledger().journalHead(), b.ledger().journalHead());
}

void fixCrc(uint8_t* bytes, size_t size) {
  SnapshotCodec::put32(bytes + size - 4, crc32(bytes, size - 4));
}

// Offset of section `id` in an encoded snapshot (0 = absent).
size_t findSection(const uint8_t* b, size_t size, uint8_t id) {
  size_t p = SnapshotCodec::kHeaderSize;
  while (p + 4 <= size - 4) {
    if ((b[p] & 0x7F) == id) return p;
    p += 4 + SnapshotCodec::get16(b + p + 2);
  }
  return 0;
}

}  // namespace

void nothing_learned_writes_nothing() {
  Personality p(kSeed);
  p.begin(0);
  uint8_t out[PersonalityCodec::kMaxSize];
  TEST_ASSERT_EQUAL_UINT32(0, PersonalityCodec::encode(p, out, sizeof(out)));
  p.ownerInput(1000);  // a session alone is not worth a section
  TEST_ASSERT_EQUAL_UINT32(0, PersonalityCodec::encode(p, out, sizeof(out)));
}

void round_trip_is_bit_identical() {
  const Personality a = learned();
  TEST_ASSERT_EQUAL_UINT32(1, a.shifts());
  TEST_ASSERT_TRUE(a.genome().offset[uint8_t(TraitId::Curiosity)] > 0);
  uint8_t first[PersonalityCodec::kMaxSize], second[PersonalityCodec::kMaxSize];
  const size_t n = PersonalityCodec::encode(a, first, sizeof(first));
  TEST_ASSERT_TRUE(n > PersonalityCodec::kFixedSize);
  TEST_ASSERT_TRUE(PersonalityCodec::valid(first, n));
  Personality b(kSeed);
  b.begin(0);
  TEST_ASSERT_TRUE(PersonalityCodec::load(first, n, b));
  sameState(a, b);
  TEST_ASSERT_EQUAL_UINT32(n, PersonalityCodec::encode(b, second, sizeof(second)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first, second, n);
  // The restored pet keeps learning exactly like the original.
  Personality c = a;
  uint32_t t = 600 * kMinute;
  for (int i = 0; i < 3; ++i) {
    petSession(c, t);
    t = petSession(b, t);
  }
  sameState(c, b);
}

void weights_are_bounded_and_keep_the_dominant() {
  Personality p(kSeed);
  p.begin(0);
  // A pathological ledger: every trait gets every reason and source.
  EvidenceLedger worst;
  for (uint8_t t = 0; t < kTraitCount; ++t)
    for (uint8_t r = 1; r < kEvidenceReasonCount; ++r)
      for (uint8_t s = 1; s < kEvidenceSourceCount; ++s)
        worst.observe(static_cast<TraitId>(t), int16_t(1000 + 100 * r + s),
                      static_cast<EvidenceSource>(s), static_cast<EvidenceReason>(r), 1);
  TraitLedger traits[kTraitCount];
  for (uint8_t t = 0; t < kTraitCount; ++t) traits[t] = worst.trait(static_cast<TraitId>(t));
  JournalEntry journal[EvidenceLedger::kJournalSize];
  int16_t offsets[kTraitCount] = {};
  p.restore(offsets, 1, 600, 0, 0, traits, journal, 0, 0);
  uint8_t out[PersonalityCodec::kMaxSize];
  const size_t n = PersonalityCodec::encode(p, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(PersonalityCodec::kMaxSize, n);  // 482: the worst case fits
  Personality q(kSeed);
  q.begin(0);
  TEST_ASSERT_TRUE(PersonalityCodec::load(out, n, q));
  for (uint8_t t = 0; t < kTraitCount; ++t) {
    const TraitLedger& a = p.ledger().trait(static_cast<TraitId>(t));
    const TraitLedger& b = q.ledger().trait(static_cast<TraitId>(t));
    uint8_t ra = 0, rb = 0, kept = 0;
    for (uint8_t r = 0; r < kEvidenceReasonCount; ++r) {
      if (a.reasonWeight[r] > a.reasonWeight[ra]) ra = r;
      if (b.reasonWeight[r] > b.reasonWeight[rb]) rb = r;
      if (b.reasonWeight[r]) ++kept;
    }
    TEST_ASSERT_EQUAL_UINT8(ra, rb);  // the journal's reason survives
    TEST_ASSERT_EQUAL_UINT8(PersonalityCodec::kMaxReasons, kept);
    TEST_ASSERT_EQUAL_INT32(a.pending, b.pending);  // the trait math is exact
    TEST_ASSERT_EQUAL_INT32(a.importance, b.importance);
  }
  // Too small a buffer never writes a partial section.
  TEST_ASSERT_EQUAL_UINT32(0, PersonalityCodec::encode(p, out, PersonalityCodec::kMaxSize - 1));
}

void invalid_sections_are_rejected() {
  const Personality a = learned();
  uint8_t b[PersonalityCodec::kMaxSize];
  const size_t n = PersonalityCodec::encode(a, b, sizeof(b));
  TEST_ASSERT_FALSE(PersonalityCodec::valid(b, n - 1));            // truncated
  TEST_ASSERT_FALSE(PersonalityCodec::valid(b, PersonalityCodec::kFixedSize - 1));
  uint8_t c[PersonalityCodec::kMaxSize];
  std::memcpy(c, b, n);
  c[16] = 0xff;
  c[17] = 0x7f;  // curiosity offset beyond ±0.6
  TEST_ASSERT_FALSE(PersonalityCodec::valid(c, n));
  std::memcpy(c, b, n);
  c[36] = EvidenceLedger::kJournalSize;  // journal head out of range
  TEST_ASSERT_FALSE(PersonalityCodec::valid(c, n));
  std::memcpy(c, b, n);
  c[PersonalityCodec::kFixedSize] = kTraitCount;  // trait id out of range
  TEST_ASSERT_FALSE(PersonalityCodec::valid(c, n));
  Personality untouched(kSeed);
  untouched.begin(0);
  TEST_ASSERT_FALSE(PersonalityCodec::load(c, n, untouched));
  TEST_ASSERT_EQUAL_UINT32(0, untouched.evidence());
}

void snapshot_section_is_optional_and_skipped_when_unreadable() {
  LifeSnapshotV2 s;
  const Personality a = learned();
  s.personality.length = static_cast<uint16_t>(
      PersonalityCodec::encode(a, s.personality.bytes, sizeof(s.personality.bytes)));
  uint8_t bytes[SnapshotCodec::kBudget];
  const size_t size = SnapshotCodec::encode(s, bytes, sizeof(bytes));
  TEST_ASSERT_TRUE(size > 0);
  const size_t at = findSection(bytes, size, uint8_t(SectionId::Personality));
  TEST_ASSERT_TRUE(at != 0);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(SectionId::Personality), bytes[at]);  // optional bit clear
  LifeSnapshotV2 back;
  SnapshotDecodeInfo info = SnapshotCodec::decode(bytes, size, back);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok), uint8_t(info.result));
  TEST_ASSERT_EQUAL_UINT16(s.personality.length, back.personality.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(s.personality.bytes, back.personality.bytes, s.personality.length);
  uint8_t again[SnapshotCodec::kBudget];
  TEST_ASSERT_EQUAL_UINT32(size, SnapshotCodec::encode(back, again, sizeof(again)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, again, size);

  // A newer section version (what an older firmware sees): skipped, Ok.
  uint8_t newer[SnapshotCodec::kBudget];
  std::memcpy(newer, bytes, size);
  newer[at + 1] = PersonalityCodec::kVersion + 1;
  fixCrc(newer, size);
  LifeSnapshotV2 old;
  info = SnapshotCodec::decode(newer, size, old);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok), uint8_t(info.result));
  TEST_ASSERT_EQUAL_UINT8(1, info.skippedSections);
  TEST_ASSERT_EQUAL_UINT16(0, old.personality.length);
  // Invalid content behind a good CRC: skipped too, never ERROR_SAFE.
  std::memcpy(newer, bytes, size);
  newer[at + 4 + 36] = 9;  // journal head
  fixCrc(newer, size);
  info = SnapshotCodec::decode(newer, size, old);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok), uint8_t(info.result));
  TEST_ASSERT_EQUAL_UINT8(1, info.skippedSections);
  TEST_ASSERT_EQUAL_UINT16(0, old.personality.length);
}

void worst_case_snapshot_fits_the_budget() {
  LifeSnapshotV2 s;
  s.clock.lastTrustedUtcMinute = 1;
  s.hasNeeds = true;
  s.habit.sessions = 1;
  s.habit.counts[0] = 1;
  const LearnerV3Config& c = s.learner.config();
  for (uint8_t i = 0; i < c.interactions; ++i)
    for (uint8_t d = 0; d < LearnerV3Config::kMaxDayParts; ++d)
      for (uint8_t a = 0; a < c.actions; ++a) s.learner.state(i, d, a, (i + d + a) % 2 == 0);
  // Maximum-length section 5: the pathological ledger again.
  Personality p(kSeed);
  EvidenceLedger worst;
  for (uint8_t t = 0; t < kTraitCount; ++t)
    for (uint8_t r = 1; r < kEvidenceReasonCount; ++r) {
      worst.observe(static_cast<TraitId>(t), 2000, EvidenceSource::OwnerReaction,
                    static_cast<EvidenceReason>(r), 1);
      worst.observe(static_cast<TraitId>(t), 2000, EvidenceSource::DeviceObserved,
                    static_cast<EvidenceReason>(r), 1);
    }
  TraitLedger traits[kTraitCount];
  for (uint8_t t = 0; t < kTraitCount; ++t) traits[t] = worst.trait(static_cast<TraitId>(t));
  JournalEntry journal[EvidenceLedger::kJournalSize];
  int16_t offsets[kTraitCount] = {};
  p.restore(offsets, 1, 1, 0, 0, traits, journal, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(PersonalityCodec::kMaxSize,
                           PersonalityCodec::encode(p, s.personality.bytes, sizeof(s.personality.bytes)));
  const size_t size = SnapshotCodec::encodedSize(s);
  TEST_ASSERT_TRUE(size <= SnapshotCodec::kBudget);
  uint8_t bytes[SnapshotCodec::kBudget];
  TEST_ASSERT_EQUAL_UINT32(size, SnapshotCodec::encode(s, bytes, sizeof(bytes)));
}

void board_keeps_personality_across_a_reboot() {
  BoardLab lab(7);
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT16(0, lab.snapshot().personality.length);  // nothing learned yet
  TEST_ASSERT_TRUE(lab.greeting(1000));
  TEST_ASSERT_TRUE(lab.greeting(3000));  // pet: evidence + an urgent commit
  lab.loop(4000);
  TEST_ASSERT_TRUE(lab.snapshot().personality.length > 0);
  // The evidence reaches flash with the next commit (the 5-minute checkpoint).
  for (uint32_t t = 5000; t <= 301000; t += 1000) lab.loop(t);
  TEST_ASSERT_TRUE(lab.checkpoints() >= 1);
  const Personality before = lab.core().personality();
  TEST_ASSERT_EQUAL_UINT32(1, before.evidence());
  TEST_ASSERT_TRUE(lab.boot(0));  // power cycle: RAM is rebuilt from the store
  const Personality& after = lab.core().personality();
  TEST_ASSERT_EQUAL_UINT32(1, after.evidence());
  TEST_ASSERT_EQUAL_UINT32(before.sessions(), after.sessions());
  TEST_ASSERT_EQUAL_INT32(before.ledger().trait(TraitId::Sociability).pending,
                          after.ledger().trait(TraitId::Sociability).pending);
  TEST_ASSERT_EQUAL_INT16_ARRAY(before.genome().baseline, after.genome().baseline, kTraitCount);

  // ForgetAll commits without section 5; a reboot does not bring it back.
  uint8_t frame[16];
  TEST_ASSERT_EQUAL_UINT8(0, lab.command(frame, BoardLab::forget(frame, 9), 302000));
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_EQUAL_UINT32(0, lab.core().personality().evidence());
  TEST_ASSERT_EQUAL_UINT16(0, lab.snapshot().personality.length);
}

void trait_consolidation_is_reported_once() {
  // A pet one session away from a curiosity step (3 strong sessions stored),
  // whose owner taught it to like being curious when quiet.
  Personality almost(kSeed);
  almost.begin(0);
  uint32_t t = kMinute;
  for (int i = 0; i < 3; ++i) t = petSession(almost, t);
  TEST_ASSERT_EQUAL_UINT32(0, almost.shifts());
  LifeSnapshotV2 stored;
  stored.personality.length = static_cast<uint16_t>(
      PersonalityCodec::encode(almost, stored.personality.bytes, sizeof(stored.personality.bytes)));
  for (uint8_t d = 0; d < LearnerV3Config::kMaxDayParts; ++d) {
    stored.learner.state(0, d, 2, true);
    stored.learner.state(0, d, 0, false);
    stored.learner.state(0, d, 1, false);
  }
  LifeCore core;
  core.restore(stored);
  core.seedPersonality(kSeed);
  TEST_ASSERT_TRUE(core.begin(0, true));
  TEST_ASSERT_EQUAL_UINT32(almost.evidence(), core.personality().evidence());

  // XUNA picks curious on its own; the owner pets it; the session ends.
  const LifeCoreEvent own(LifeCoreEventKind::Spontaneous);
  const LifeCoreStepOutput shown = core.step(LifeCoreStepInput(1000, &own, 1));
  TEST_ASSERT_TRUE(shown.decisionShown);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ActionId::Curious), shown.selectedAction);
  const LifeCoreEvent pet(LifeCoreEventKind::Reaction, 0, 0, 0, true);
  TEST_ASSERT_TRUE(core.step(LifeCoreStepInput(3000, &pet, 1)).reactionApplied);
  uint8_t reported = 0;
  for (uint32_t now = 60000; now <= 40 * kMinute; now += 60000)
    reported = static_cast<uint8_t>(reported + core.step(LifeCoreStepInput(now)).traitsConsolidated);
  TEST_ASSERT_EQUAL_UINT8(1, reported);  // once, in the step that closed the session
  TEST_ASSERT_EQUAL_UINT32(1, core.personality().shifts());
  TEST_ASSERT_EQUAL_UINT16(1, core.personality().ledger().journal(0).sequence);
  // Section 5 of the RAM snapshot already holds the step for the next commit.
  Personality check(kSeed);
  TEST_ASSERT_TRUE(PersonalityCodec::load(core.snapshot().personality.bytes,
                                          core.snapshot().personality.length, check));
  TEST_ASSERT_EQUAL_UINT32(1, check.shifts());
  TEST_ASSERT_EQUAL_INT16(core.personality().genome().offset[uint8_t(TraitId::Curiosity)],
                          check.genome().offset[uint8_t(TraitId::Curiosity)]);
}

void projection_reads_the_personality() {
  const Personality p = learned();
  uint8_t b[PersonalityProjection::kSize];
  PersonalityProjection::encode(p, true, b);
  TEST_ASSERT_EQUAL_UINT8(1, b[0]);
  TEST_ASSERT_EQUAL_UINT8(PersonalityProjection::kSize, b[1]);
  TEST_ASSERT_EQUAL_UINT8(1, b[2]);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Expression::traitWord(p.genome())), b[3]);
  const int16_t curiosity = static_cast<int16_t>(b[6] | (b[7] << 8));
  TEST_ASSERT_EQUAL_INT16(Temperament::effective(p.genome(), TraitId::Curiosity), curiosity);
  TEST_ASSERT_EQUAL_UINT16(p.sessions(), uint16_t(b[30] | (b[31] << 8)));
  // Newest journal entry first: the curiosity step.
  TEST_ASSERT_EQUAL_UINT16(1, uint16_t(b[32] | (b[33] << 8)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitId::Curiosity), b[34]);
  TEST_ASSERT_EQUAL_INT16(EvidenceLedger::kMaxStep, int16_t(b[38] | (b[39] << 8)));
  TEST_ASSERT_EQUAL_UINT8(0, b[40]);  // second slot empty
  PersonalityProjection::encode(Personality(), false, b);
  TEST_ASSERT_EQUAL_UINT8(0, b[2]);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(TraitWord::Balanced), b[3]);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(nothing_learned_writes_nothing);
  RUN_TEST(round_trip_is_bit_identical);
  RUN_TEST(weights_are_bounded_and_keep_the_dominant);
  RUN_TEST(invalid_sections_are_rejected);
  RUN_TEST(snapshot_section_is_optional_and_skipped_when_unreadable);
  RUN_TEST(worst_case_snapshot_fits_the_budget);
  RUN_TEST(board_keeps_personality_across_a_reboot);
  RUN_TEST(trait_consolidation_is_reported_once);
  RUN_TEST(projection_reads_the_personality);
  return UNITY_END();
}
