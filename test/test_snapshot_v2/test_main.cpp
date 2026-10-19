#include <unity.h>

#include <cstring>
#include <vector>

#include <xuna/persist/Migrations.h>
#include <xuna/persist/SnapshotCodec.h>
#include <xuna/persist/SnapshotStore.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

namespace {

typedef std::vector<uint8_t> Bytes;

V3Decision shown(uint8_t interaction, uint8_t dayPart, uint8_t action,
                 uint32_t id) {
  V3Decision d;
  d.id = id;
  d.interaction = interaction;
  d.dayPart = dayPart;
  d.action = action;
  return d;
}

LifeSnapshotV2 trained(uint32_t generation, uint32_t ratings) {
  LifeSnapshotV2 s;
  s.generation = generation;
  s.counters.bootCounter = 12;
  s.counters.activeSeconds = 3600 + ratings;
  s.counters.eventSeqCommitted = 0;
  s.counters.feedbackCount = ratings * 3;
  uint32_t id = 1;
  for (uint32_t i = 0; i < ratings; ++i) {
    s.learner.feedback(shown(0, 0, 1, id++), true);
    s.learner.feedback(shown(1, 3, 0, id++), i % 3 != 0);
    s.learner.feedback(shown(2, 2, 2, id++), i % 2 == 0);
  }
  s.episodes.count = 3;
  s.episodes.next = 3;
  for (uint8_t i = 0; i < 3; ++i) {
    s.episodes.items[i].context = i;
    s.episodes.items[i].action = static_cast<uint8_t>(2 - i);
    s.episodes.items[i].approved = i % 2;
    s.episodes.items[i].source = 1 + i % 2;
  }
  return s;
}

Bytes encoded(const LifeSnapshotV2& s) {
  Bytes out(SnapshotCodec::kBudget);
  const size_t size = SnapshotCodec::encode(s, out.data(), out.size());
  out.resize(size);
  return out;
}

// Builds raw snapshots with a valid CRC to reach each decode branch.
struct Raw {
  Bytes b;
  explicit Raw(uint8_t schema = 2, uint8_t flags = 0, uint32_t generation = 7) {
    const uint8_t header[12] = {'X', 'U', schema, flags, 0, 0, 0, 0, 0, 0, 0, 0};
    b.assign(header, header + 12);
    SnapshotCodec::put32(&b[8], generation);
  }
  Raw& section(uint8_t idByte, uint8_t version, const Bytes& data) {
    b.push_back(idByte);
    b.push_back(version);
    b.push_back(static_cast<uint8_t>(data.size()));
    b.push_back(static_cast<uint8_t>(data.size() >> 8));
    b.insert(b.end(), data.begin(), data.end());
    return *this;
  }
  Raw& counters() { return section(0x81, 1, Bytes(16, 0)); }
  Raw& learner(const LearnerV3Config& config = productLearnerConfig()) {
    const PreferenceLearnerV3 l(config);
    Bytes data(LearnerV3Codec::encodedSize(config));
    LearnerV3Codec::encode(l, data.data(), data.size());
    return section(0x86, 1, data);
  }
  Bytes done() const {
    Bytes out = b;
    const size_t size = out.size() + 4;
    out[4] = static_cast<uint8_t>(size);
    out[5] = static_cast<uint8_t>(size >> 8);
    const uint32_t crc = crc32(out.data(), out.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(crc >> (8 * i)));
    return out;
  }
};

LoadResult decodeResult(const Bytes& bytes) {
  LifeSnapshotV2 out;
  return SnapshotCodec::decode(bytes.data(), bytes.size(), out).result;
}

enum class CutMode { Overlay, Erased };

// In-memory store. A cut write leaves the new length with only the first
// `cutAt` new bytes; the rest keeps old bytes (Overlay) or 0xFF (Erased).
class FakeStore : public ISnapshotStore {
 public:
  bool exists[4] = {false, false, false, false};
  Bytes slots[4];
  long cutAt = -1;
  CutMode mode = CutMode::Overlay;
  int writes = 0;
  SnapshotKey lastKey = SnapshotKey::V1Slot0;

  void put(SnapshotKey key, const Bytes& bytes) {
    exists[int(key)] = true;
    slots[int(key)] = bytes;
  }
  bool read(SnapshotKey key, uint8_t* out, size_t capacity, size_t& size) override {
    if (!exists[int(key)]) return false;
    const Bytes& s = slots[int(key)];
    size = s.size();
    std::memcpy(out, s.data(), size < capacity ? size : capacity);
    return true;
  }
  bool write(SnapshotKey key, const uint8_t* bytes, size_t size) override {
    ++writes;
    lastKey = key;
    Bytes& slot = slots[int(key)];
    if (cutAt >= 0 && size_t(cutAt) < size) {
      Bytes torn(size, 0xFF);
      for (size_t i = 0; i < size; ++i) {
        if (i < size_t(cutAt)) torn[i] = bytes[i];
        else if (mode == CutMode::Overlay && i < slot.size()) torn[i] = slot[i];
      }
      slot = torn;
      exists[int(key)] = true;
      return false;
    }
    slot.assign(bytes, bytes + size);
    exists[int(key)] = true;
    return true;
  }
};

Bytes v1Bytes(const uint8_t* fixture) {
  return Bytes(fixture, fixture + LifeEngine::kSnapshotSize);
}

Bytes recoveredBytes(FakeStore& store, LoadResult& result) {
  LifeSnapshotV2 out;
  result = SnapshotStore::recover(store, out).result;
  return encoded(out);
}

}  // namespace

// ---- S12a: codec ----------------------------------------------------------

void test_roundtrip_product_shape() {
  LifeSnapshotV2 s = trained(41, 9);
  s.hasNeeds = true;
  s.needs.energy = 41943;
  s.needs.stimulation = 4096;
  const Bytes bytes = encoded(s);
  LifeSnapshotV2 out;
  const SnapshotDecodeInfo info = SnapshotCodec::decode(bytes.data(), bytes.size(), out);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
  TEST_ASSERT_EQUAL_UINT32(41, info.generation);
  TEST_ASSERT_EQUAL_UINT32(s.learner.fingerprint(), out.learner.fingerprint());
  TEST_ASSERT_EQUAL_UINT32(s.learner.policyVersion(), out.learner.policyVersion());
  TEST_ASSERT_TRUE(out.hasNeeds);
  TEST_ASSERT_EQUAL_UINT16(41943, out.needs.energy);
  TEST_ASSERT_EQUAL_UINT16(4096, out.needs.stimulation);
  TEST_ASSERT_EQUAL_UINT32(12, out.counters.bootCounter);
  TEST_ASSERT_EQUAL_UINT32(27, out.counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(3, out.episodes.count);
  TEST_ASSERT_EQUAL_UINT8(2, out.episodes.items[2].context);
  const Bytes again = encoded(out);
  TEST_ASSERT_TRUE(bytes == again);
}

void test_size_within_budget() {
  LifeSnapshotV2 s;
  TEST_ASSERT_EQUAL_UINT32(536, SnapshotCodec::encodedSize(s));
  s.hasNeeds = true;
  TEST_ASSERT_EQUAL_UINT32(544, SnapshotCodec::encodedSize(s));
  TEST_ASSERT_TRUE(SnapshotCodec::encodedSize(s) <= SnapshotCodec::kBudget);
  uint8_t small[100];
  TEST_ASSERT_EQUAL_UINT32(0, SnapshotCodec::encode(s, small, sizeof(small)));
  // The largest learner the type allows (4 × 5 × 8) does not fit the budget.
  LearnerV3Config big = productLearnerConfig();
  big.interactions = LearnerV3Config::kMaxInteractions;
  big.actions = LearnerV3Config::kMaxActions;
  LifeSnapshotV2 tooBig(big);
  uint8_t buffer[4096];
  TEST_ASSERT_EQUAL_UINT32(0, SnapshotCodec::encode(tooBig, buffer, sizeof(buffer)));
}

void test_every_bit_flip_is_corrupt_and_output_unchanged() {
  const Bytes bytes = encoded(trained(5, 4));
  LifeSnapshotV2 out = trained(99, 2);
  const uint32_t before = out.learner.fingerprint();
  for (size_t i = 0; i < bytes.size(); ++i) {
    Bytes flipped = bytes;
    flipped[i] ^= static_cast<uint8_t>(1U << (i % 8));
    const SnapshotDecodeInfo info = SnapshotCodec::decode(flipped.data(), flipped.size(), out);
    TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(info.result));
  }
  TEST_ASSERT_EQUAL_UINT32(99, out.generation);
  TEST_ASSERT_EQUAL_UINT32(before, out.learner.fingerprint());
}

void test_truncated_or_padded_is_corrupt() {
  const Bytes bytes = encoded(trained(5, 4));
  for (size_t n = 0; n < bytes.size(); n += 37)
    TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Bytes(bytes.begin(), bytes.begin() + n))));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Bytes(bytes.begin(), bytes.end() - 1))));
  Bytes padded = bytes;
  padded.push_back(0);
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(padded)));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(v1Bytes(xuna_fixture::scripted))));
}

void test_newer_schema_or_flags_is_too_new() {
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(decodeResult(Raw().counters().learner().done())));
  TEST_ASSERT_EQUAL(int(LoadResult::TooNew), int(decodeResult(Raw(3).counters().learner().done())));
  TEST_ASSERT_EQUAL(int(LoadResult::TooNew), int(decodeResult(Raw(2, 1).counters().learner().done())));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw(1).counters().learner().done())));
}

void test_unknown_sections_follow_required_bit() {
  const Bytes extra(5, 0xAB);
  // Unknown required id, and a known required section at a newer version.
  TEST_ASSERT_EQUAL(int(LoadResult::TooNew), int(decodeResult(Raw().counters().learner().section(0x8D, 1, extra).done())));
  TEST_ASSERT_EQUAL(int(LoadResult::TooNew), int(decodeResult(Raw().section(0x81, 2, Bytes(20, 0)).learner().done())));
  // Unknown optional id and a newer optional section are skipped.
  LifeSnapshotV2 out;
  const Bytes skipped = Raw().counters().section(0x0D, 1, extra).section(0x04, 2, Bytes(6, 1)).learner().done();
  const SnapshotDecodeInfo info = SnapshotCodec::decode(skipped.data(), skipped.size(), out);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
  TEST_ASSERT_EQUAL_UINT8(2, info.skippedSections);
  TEST_ASSERT_FALSE(out.hasNeeds);
}

void test_missing_optional_sections_load_defaults() {
  LifeSnapshotV2 out = trained(3, 5);
  out.hasNeeds = true;
  const Bytes bytes = Raw(2, 0, 77).counters().learner().done();
  const SnapshotDecodeInfo info = SnapshotCodec::decode(bytes.data(), bytes.size(), out);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
  TEST_ASSERT_EQUAL_UINT32(77, out.generation);
  TEST_ASSERT_FALSE(out.hasNeeds);
  TEST_ASSERT_EQUAL_UINT8(0, out.episodes.count);
  TEST_ASSERT_EQUAL_UINT32(PreferenceLearnerV3(productLearnerConfig()).fingerprint(), out.learner.fingerprint());
}

void test_structural_errors_are_corrupt() {
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().learner().done())));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().done())));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().counters().learner().done())));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().learner().section(0x00, 1, Bytes(2, 0)).done())));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().learner().section(0x0D, 0, Bytes(2, 0)).done())));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().learner().section(0x04, 1, Bytes(3, 0)).done())));
  Bytes episodes(34, 0);
  episodes[0] = 1;  // one stored episode with source 0
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().learner().section(0x0B, 1, episodes).done())));
  episodes[0] = 0;
  episodes[2] = 3;  // context outside the D5 vocabulary
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().learner().section(0x0B, 1, episodes).done())));
  // A section length that runs past the trailer.
  Bytes overrun = Raw().counters().learner().done();
  overrun[12 + 2] = 0xFF;
  const uint32_t crc = crc32(overrun.data(), overrun.size() - 4);
  for (int i = 0; i < 4; ++i) overrun[overrun.size() - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(overrun)));
}

void test_learner_shape_changes_keep_or_drop_cells() {
  // A 2 × 5 × 2 learner (v1-compatible trial shape) loads into the product
  // shape; the new interaction/action cells start empty.
  PreferenceLearnerV3 small(v1CompatibleLearnerConfig());
  small.feedback(shown(1, 0, 1, 1), true);
  Bytes data(LearnerV3Codec::encodedSize(small.config()));
  LearnerV3Codec::encode(small, data.data(), data.size());
  const Bytes bytes = Raw().counters().section(0x86, 1, data).done();
  LifeSnapshotV2 out;
  SnapshotDecodeInfo info = SnapshotCodec::decode(bytes.data(), bytes.size(), out);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
  TEST_ASSERT_EQUAL_UINT16(0, info.droppedLearnerCells);
  TEST_ASSERT_EQUAL_UINT16(1, out.learner.view(1, 0, 1).rated);
  TEST_ASSERT_EQUAL_UINT16(0, out.learner.view(2, 0, 2).rated);
  // Product cells outside a smaller RAM shape are counted as dropped.
  const Bytes product = encoded(trained(1, 3));
  LifeSnapshotV2 narrow(v1CompatibleLearnerConfig());
  info = SnapshotCodec::decode(product.data(), product.size(), narrow);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(info.result));
  TEST_ASSERT_TRUE(info.droppedLearnerCells > 0);
}

// S10: the trusted-time watermark is stored only after a sync.
void test_clock_section_roundtrip_and_validation() {
  LifeSnapshotV2 s = trained(3, 2);
  TEST_ASSERT_EQUAL_UINT32(536, encoded(s).size());
  s.clock.lastTrustedUtcMinute = 3600000;
  s.clock.tzOffsetMin = 420;
  const Bytes bytes = encoded(s);
  TEST_ASSERT_EQUAL_UINT32(546, bytes.size());
  LifeSnapshotV2 out;
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(SnapshotCodec::decode(bytes.data(), bytes.size(), out).result));
  TEST_ASSERT_EQUAL_UINT32(3600000, out.clock.lastTrustedUtcMinute);
  TEST_ASSERT_EQUAL_INT16(420, out.clock.tzOffsetMin);
  // Negative offsets survive the u16 encoding.
  s.clock.tzOffsetMin = -300;
  const Bytes west = encoded(s);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(SnapshotCodec::decode(west.data(), west.size(), out).result));
  TEST_ASSERT_EQUAL_INT16(-300, out.clock.tzOffsetMin);

  Bytes clock(6, 0);
  SnapshotCodec::put32(clock.data(), 100);
  SnapshotCodec::put16(clock.data() + 4, 900);  // beyond ±840
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().section(0x02, 1, clock).learner().done())));
  SnapshotCodec::put32(clock.data(), 0);  // stored but never synced
  SnapshotCodec::put16(clock.data() + 4, 0);
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(decodeResult(Raw().counters().section(0x02, 1, clock).learner().done())));
}

// ---- S12b: migration and recovery ----------------------------------------

void test_golden_v1_fixtures_migrate() {
  LifeSnapshotV2 fresh;
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(fromV1(xuna_fixture::fresh, 96, fresh)));
  TEST_ASSERT_EQUAL_UINT32(0, fresh.counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT8(0, fresh.episodes.count);
  TEST_ASSERT_FALSE(fresh.hasNeeds);

  LifeSnapshotV2 scripted;
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(fromV1(xuna_fixture::scripted, 96, scripted)));
  TEST_ASSERT_EQUAL_UINT32(4, scripted.counters.feedbackCount);
  TEST_ASSERT_EQUAL_UINT32(0, scripted.counters.bootCounter);
  V3CellView quietIdle = scripted.learner.view(0, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(0, quietIdle.positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(65536, quietIdle.negativeQ16);
  V3CellView playHappy = scripted.learner.view(1, 0, 1);
  TEST_ASSERT_EQUAL_UINT32(3U << 16, playHappy.positiveQ16);
  TEST_ASSERT_EQUAL_UINT16(3, playHappy.rated);
  TEST_ASSERT_EQUAL_UINT8(4, scripted.episodes.count);
  TEST_ASSERT_EQUAL_UINT8(4, scripted.episodes.next);
  TEST_ASSERT_EQUAL_UINT8(1, scripted.episodes.items[3].context);
  TEST_ASSERT_EQUAL_UINT8(2, scripted.episodes.items[3].source);

  // 1000:1000 per cell is capped at 1/(1 − γ) ≈ 10 with the ratio kept.
  LifeSnapshotV2 saturated;
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(fromV1(xuna_fixture::saturated, 96, saturated)));
  const V3CellView cell = saturated.learner.view(1, 0, 1);
  const uint32_t cap = LearnerV3Codec::effectiveMemoryQ16(productLearnerConfig().gammaQ16);
  TEST_ASSERT_UINT32_WITHIN(1, cap / 2, cell.positiveQ16);
  TEST_ASSERT_EQUAL_UINT32(cap, cell.positiveQ16 + cell.negativeQ16);
  TEST_ASSERT_EQUAL_UINT16(2000, cell.rated);

  Bytes broken = v1Bytes(xuna_fixture::scripted);
  broken[20] ^= 1;
  LifeSnapshotV2 untouched = trained(9, 1);
  TEST_ASSERT_EQUAL(int(LoadResult::MigrationFailed), int(fromV1(broken.data(), broken.size(), untouched)));
  TEST_ASSERT_EQUAL_UINT32(9, untouched.generation);
}

void test_migration_is_idempotent_and_encodes() {
  LifeSnapshotV2 first, second;
  fromV1(xuna_fixture::scripted, 96, first);
  fromV1(xuna_fixture::scripted, 96, second);
  fromV1(xuna_fixture::scripted, 96, second);
  const Bytes a = encoded(first), b = encoded(second);
  TEST_ASSERT_TRUE(a == b);
  LifeSnapshotV2 decoded;
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(SnapshotCodec::decode(a.data(), a.size(), decoded).result));
  TEST_ASSERT_TRUE(encoded(decoded) == a);
}

void test_recover_selects_by_rule() {
  LoadResult r;
  FakeStore empty;
  recoveredBytes(empty, r);
  TEST_ASSERT_EQUAL(int(LoadResult::Missing), int(r));

  FakeStore v1Only;
  v1Only.put(SnapshotKey::V1Slot1, v1Bytes(xuna_fixture::scripted));
  LifeSnapshotV2 migrated;
  fromV1(xuna_fixture::scripted, 96, migrated);
  TEST_ASSERT_TRUE(recoveredBytes(v1Only, r) == encoded(migrated));
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(r));

  FakeStore v1Broken;
  v1Broken.put(SnapshotKey::V1Slot0, Bytes(96, 0x11));
  recoveredBytes(v1Broken, r);
  TEST_ASSERT_EQUAL(int(LoadResult::MigrationFailed), int(r));

  // Newest generation wins, also across the 32-bit wrap.
  FakeStore wrap;
  wrap.put(SnapshotKey::V2Slot1, encoded(trained(0xFFFFFFFFU, 2)));
  wrap.put(SnapshotKey::V2Slot0, encoded(trained(0, 3)));
  wrap.put(SnapshotKey::V1Slot0, v1Bytes(xuna_fixture::scripted));
  TEST_ASSERT_TRUE(recoveredBytes(wrap, r) == encoded(trained(0, 3)));
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(r));

  // One unreadable slot: the other one is used and the loss is reported.
  FakeStore oneBad;
  oneBad.put(SnapshotKey::V2Slot0, encoded(trained(8, 3)));
  oneBad.put(SnapshotKey::V2Slot1, Bytes(200, 0x5A));
  LifeSnapshotV2 out;
  const RecoveryReport report = SnapshotStore::recover(oneBad, out);
  TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(report.result));
  TEST_ASSERT_EQUAL_INT8(0, report.slot);
  TEST_ASSERT_EQUAL_UINT8(1, report.corruptSlots);

  // Both v2 slots unreadable: no silent rollback to v1.
  FakeStore bothBad;
  bothBad.put(SnapshotKey::V2Slot0, Bytes(64, 0));
  bothBad.put(SnapshotKey::V2Slot1, Bytes(64, 1));
  bothBad.put(SnapshotKey::V1Slot0, v1Bytes(xuna_fixture::scripted));
  recoveredBytes(bothBad, r);
  TEST_ASSERT_EQUAL(int(LoadResult::Corrupt), int(r));

  // An intact newer schema in either slot wins over older valid data.
  FakeStore tooNew;
  tooNew.put(SnapshotKey::V2Slot0, encoded(trained(50, 2)));
  tooNew.put(SnapshotKey::V2Slot1, Raw(3, 0, 51).counters().learner().done());
  LifeSnapshotV2 kept = trained(1, 1);
  TEST_ASSERT_EQUAL(int(LoadResult::TooNew), int(SnapshotStore::recover(tooNew, kept).result));
  TEST_ASSERT_EQUAL_UINT32(1, kept.generation);

  // A single unreadable v2 slot (torn first write) falls back to v1.
  FakeStore firstTorn;
  firstTorn.put(SnapshotKey::V2Slot1, Bytes(120, 0xFF));
  firstTorn.put(SnapshotKey::V1Slot0, v1Bytes(xuna_fixture::scripted));
  TEST_ASSERT_TRUE(recoveredBytes(firstTorn, r) == encoded(migrated));
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(r));

  // The same torn first write on a new pet without v1 starts fresh.
  FakeStore freshTorn;
  freshTorn.put(SnapshotKey::V2Slot0, Bytes(120, 0xFF));
  LifeSnapshotV2 fresh;
  const RecoveryReport freshReport = SnapshotStore::recover(freshTorn, fresh);
  TEST_ASSERT_EQUAL(int(LoadResult::Missing), int(freshReport.result));
  TEST_ASSERT_EQUAL_UINT8(1, freshReport.corruptSlots);
}

void test_commit_alternates_slots() {
  FakeStore store;
  store.put(SnapshotKey::V1Slot0, v1Bytes(xuna_fixture::scripted));
  LifeSnapshotV2 ram;
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(SnapshotStore::recover(store, ram).result));
  for (uint32_t i = 0; i < 4; ++i) {
    LifeSnapshotV2 staged = ram;
    staged.learner.feedback(shown(2, 0, 2, 100 + i), true);
    TEST_ASSERT_TRUE(SnapshotStore::commit(store, staged));
    TEST_ASSERT_EQUAL(int(staged.generation % 2 ? SnapshotKey::V2Slot1 : SnapshotKey::V2Slot0), int(store.lastKey));
    ram = staged;
    LifeSnapshotV2 rebooted;
    const RecoveryReport report = SnapshotStore::recover(store, rebooted);
    TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(report.result));
    TEST_ASSERT_TRUE(encoded(rebooted) == encoded(ram));
  }
  // v1 slots stay untouched (rollback point).
  TEST_ASSERT_TRUE(store.slots[int(SnapshotKey::V1Slot0)] == v1Bytes(xuna_fixture::scripted));
}

// Every cut point of a commit between two committed v2 generations recovers
// exactly the previous state or (only when complete) the new one.
void test_torn_write_sweep_between_generations() {
  const CutMode modes[2] = {CutMode::Overlay, CutMode::Erased};
  FakeStore base;
  LifeSnapshotV2 ram = trained(10, 2);
  TEST_ASSERT_TRUE(SnapshotStore::commit(base, ram));
  LifeSnapshotV2 second = ram;
  second.learner.feedback(shown(0, 1, 0, 900), false);
  TEST_ASSERT_TRUE(SnapshotStore::commit(base, second));
  const Bytes previous = encoded(second);
  LifeSnapshotV2 next = trained(0, 7);  // different learner and size
  next.generation = second.generation;
  next.hasNeeds = true;
  next.needs.energy = 30000;
  int completed = 0;
  for (int m = 0; m < 2; ++m) {
    for (long cut = 0;; ++cut) {
      FakeStore store = base;
      store.cutAt = cut;
      store.mode = modes[m];
      LifeSnapshotV2 staged = next;
      const bool ok = SnapshotStore::commit(store, staged);
      LoadResult r;
      const Bytes got = recoveredBytes(store, r);
      TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(r));
      if (ok) {
        TEST_ASSERT_TRUE(got == encoded(staged));
        ++completed;
        break;
      }
      TEST_ASSERT_TRUE(got == previous || got == encoded(staged));
    }
  }
  TEST_ASSERT_EQUAL(2, completed);
}

// The first v2 write after an upgrade can be cut anywhere: the pet then
// boots from the untouched v1 slots again.
void test_torn_first_v2_write_falls_back_to_v1() {
  FakeStore base;
  base.put(SnapshotKey::V1Slot0, v1Bytes(xuna_fixture::scripted));
  LifeSnapshotV2 migrated;
  TEST_ASSERT_EQUAL(int(LoadResult::Migrated), int(SnapshotStore::recover(base, migrated).result));
  const Bytes migratedBytes = encoded(migrated);
  for (int m = 0; m < 2; ++m) {
    for (long cut = 0;; ++cut) {
      FakeStore store = base;
      store.cutAt = cut;
      store.mode = m ? CutMode::Erased : CutMode::Overlay;
      LifeSnapshotV2 staged = migrated;
      const bool ok = SnapshotStore::commit(store, staged);
      LoadResult r;
      const Bytes got = recoveredBytes(store, r);
      if (ok) {
        TEST_ASSERT_EQUAL(int(LoadResult::Ok), int(r));
        TEST_ASSERT_TRUE(got == encoded(staged));
        break;
      }
      TEST_ASSERT_TRUE((r == LoadResult::Migrated && got == migratedBytes) ||
                       (r == LoadResult::Ok && got == encoded(staged)));
    }
  }
}

void test_failed_commit_keeps_ram_and_retries_same_slot() {
  FakeStore store;
  LifeSnapshotV2 ram = trained(20, 2);
  TEST_ASSERT_TRUE(SnapshotStore::commit(store, ram));
  const SnapshotKey newest = store.lastKey;
  LifeSnapshotV2 staged = ram;
  store.cutAt = 10;
  TEST_ASSERT_FALSE(SnapshotStore::commit(store, staged));
  const SnapshotKey torn = store.lastKey;
  TEST_ASSERT_TRUE(torn != newest);
  // Caller discards `staged`; the retry from RAM targets the same slot.
  store.cutAt = -1;
  staged = ram;
  TEST_ASSERT_TRUE(SnapshotStore::commit(store, staged));
  TEST_ASSERT_EQUAL(int(torn), int(store.lastKey));
  TEST_ASSERT_EQUAL_UINT32(ram.generation + 1, staged.generation);
}

void setUp() {}
void tearDown() {}
// Owner reactions (source 3) bump only the optional episodes section to v2:
// older readers skip it, and snapshots without reactions stay v1 bytes.
uint8_t episodesVersion(const uint8_t* bytes, size_t size) {
  size_t p = SnapshotCodec::kHeaderSize;
  while (p + 4 <= size) {
    if ((bytes[p] & 0x7F) == uint8_t(SectionId::Episodes)) return bytes[p + 1];
    p += 4 + size_t(bytes[p + 2] | (bytes[p + 3] << 8));
  }
  return 0;
}

void reaction_episodes_use_section_v2_only_when_present() {
  LifeSnapshotV2 s;
  s.episodes.count = 1;
  s.episodes.next = 1;
  s.episodes.items[0].source = 1;
  uint8_t bytes[SnapshotCodec::kBudget];
  size_t size = SnapshotCodec::encode(s, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_UINT8(1, episodesVersion(bytes, size));
  s.episodes.items[0].source = 3;
  size = SnapshotCodec::encode(s, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_UINT8(2, episodesVersion(bytes, size));
  LifeSnapshotV2 back;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok),
                          uint8_t(SnapshotCodec::decode(bytes, size, back).result));
  TEST_ASSERT_EQUAL_UINT8(3, back.episodes.items[0].source);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_product_shape);
  RUN_TEST(test_size_within_budget);
  RUN_TEST(test_every_bit_flip_is_corrupt_and_output_unchanged);
  RUN_TEST(test_truncated_or_padded_is_corrupt);
  RUN_TEST(test_newer_schema_or_flags_is_too_new);
  RUN_TEST(test_unknown_sections_follow_required_bit);
  RUN_TEST(test_missing_optional_sections_load_defaults);
  RUN_TEST(test_structural_errors_are_corrupt);
  RUN_TEST(test_learner_shape_changes_keep_or_drop_cells);
  RUN_TEST(test_clock_section_roundtrip_and_validation);
  RUN_TEST(test_golden_v1_fixtures_migrate);
  RUN_TEST(test_migration_is_idempotent_and_encodes);
  RUN_TEST(test_recover_selects_by_rule);
  RUN_TEST(test_commit_alternates_slots);
  RUN_TEST(test_torn_write_sweep_between_generations);
  RUN_TEST(test_torn_first_v2_write_falls_back_to_v1);
  RUN_TEST(test_failed_commit_keeps_ram_and_retries_same_slot);
  RUN_TEST(reaction_episodes_use_section_v2_only_when_present);
  return UNITY_END();
}
