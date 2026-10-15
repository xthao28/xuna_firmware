#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/persist/Migrations.h>
#include <xuna/persist/SnapshotCodec.h>

namespace xuna {

// Logical snapshot slots. The S13 adapter maps them to the medium chosen by
// D2 (today's plan: NVS keys slot0/slot1 for v1, life0/life1 for v2). v1
// slots are only read here: they stay untouched for one release as the
// rollback point (plan M04.4).
enum class SnapshotKey : uint8_t { V1Slot0 = 0, V1Slot1, V2Slot0, V2Slot1 };

class ISnapshotStore {
 public:
  virtual ~ISnapshotStore() {}
  // false = key absent. Otherwise copies at most `capacity` bytes and sets
  // `size` to the stored length (which may exceed `capacity`).
  virtual bool read(SnapshotKey key, uint8_t* out, size_t capacity,
                    size_t& size) = 0;
  // false = the write did not complete (the slot content is then unknown).
  virtual bool write(SnapshotKey key, const uint8_t* bytes, size_t size) = 0;
};

// Durable-commit port for LifeSnapshot v2 (plan §9.5). The core stages the
// whole snapshot with the change applied and adopts it only on true. The
// adapter stamps the next generation into `staged`.
class ISnapshotCommit {
 public:
  virtual ~ISnapshotCommit() {}
  virtual bool commit(LifeSnapshotV2& staged) = 0;
};

struct RecoveryReport {
  LoadResult result = LoadResult::Missing;
  int8_t slot = -1;          // v2 slot loaded, -1 otherwise
  uint8_t corruptSlots = 0;  // v2 slots present but unreadable
  uint16_t droppedLearnerCells = 0;
};

class SnapshotStore {
 public:
  // Boot recovery. Newest valid v2 slot wins (generation, wrap-safe); slots
  // are never mixed. Any intact newer-schema slot → TooNew (no rollback
  // onto older data). Both v2 slots present and unreadable → Corrupt. With
  // no readable v2 slot and at most one present (a torn first v2 write),
  // the v1 slots are migrated (Migrated/MigrationFailed); no v1 → Missing
  // (fresh pet), with `corruptSlots` reporting a torn lone v2 slot. `out`
  // changes only for Ok and Migrated.
  static RecoveryReport recover(ISnapshotStore& store, LifeSnapshotV2& out) {
    RecoveryReport report;
    uint8_t buffer[SnapshotCodec::kBudget];
    SnapshotDecodeInfo info[2];
    bool present[2] = {false, false};
    {
      LifeSnapshotV2 scratch(out.learner.config());
      for (uint8_t slot = 0; slot < 2; ++slot) {
        size_t size = 0;
        present[slot] = store.read(v2Key(slot), buffer, sizeof(buffer), size);
        if (!present[slot]) continue;
        if (size <= sizeof(buffer))
          info[slot] = SnapshotCodec::decode(buffer, size, scratch);
        if (info[slot].result == LoadResult::TooNew) {
          report.result = LoadResult::TooNew;
          return report;
        }
        if (info[slot].result != LoadResult::Ok) ++report.corruptSlots;
      }
    }

    int8_t best = -1;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      if (info[slot].result != LoadResult::Ok) continue;
      if (best < 0 || int32_t(info[slot].generation - info[best].generation) > 0)
        best = static_cast<int8_t>(slot);
    }
    if (best >= 0) {
      size_t size = 0;
      SnapshotDecodeInfo loaded;
      if (store.read(v2Key(uint8_t(best)), buffer, sizeof(buffer), size) &&
          size <= sizeof(buffer))
        loaded = SnapshotCodec::decode(buffer, size, out);
      report.result = loaded.result == LoadResult::Ok ? LoadResult::Ok
                                                      : LoadResult::Corrupt;
      report.slot = report.result == LoadResult::Ok ? best : -1;
      report.droppedLearnerCells = loaded.droppedLearnerCells;
      return report;
    }
    if (present[0] && present[1]) {
      report.result = LoadResult::Corrupt;
      return report;
    }

    uint8_t v1[2][LifeEngine::kSnapshotSize] = {};
    size_t v1Size[2] = {0, 0};
    bool v1Present = false;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      size_t size = 0;
      const SnapshotKey key = slot ? SnapshotKey::V1Slot1 : SnapshotKey::V1Slot0;
      if (!store.read(key, v1[slot], sizeof(v1[slot]), size)) continue;
      v1Present = true;
      v1Size[slot] = size == LifeEngine::kSnapshotSize ? size : 0;
    }
    if (!v1Present) {
      // A lone unreadable v2 slot without v1 is a torn first write of a new
      // pet: nothing was ever committed, so start fresh (corruptSlots says so).
      report.result = LoadResult::Missing;
      return report;
    }
    LifeEngine life;
    if (!life.recover(v1[0], v1Size[0], v1[1], v1Size[1])) {
      report.result = LoadResult::MigrationFailed;
      return report;
    }
    report.result = migrateV1(life, out) ? LoadResult::Migrated
                                         : LoadResult::MigrationFailed;
    return report;
  }

  // Commit-first write (plan §9.5): stamps the next generation into `staged`
  // and writes it to slot generation % 2, which never holds the newest valid
  // snapshot. The caller adopts `staged` only when this returns true; on
  // false RAM keeps the previous state and a retry targets the same slot.
  static bool commit(ISnapshotStore& store, LifeSnapshotV2& staged) {
    uint8_t buffer[SnapshotCodec::kBudget];
    ++staged.generation;
    const size_t size = SnapshotCodec::encode(staged, buffer, sizeof(buffer));
    if (size == 0) return false;
    return store.write(v2Key(uint8_t(staged.generation % 2U)), buffer, size);
  }

 private:
  static SnapshotKey v2Key(uint8_t slot) {
    return slot ? SnapshotKey::V2Slot1 : SnapshotKey::V2Slot0;
  }
};

// Commit port backed by a snapshot store (lab, and the S13 adapter).
class StoreSnapshotCommit : public ISnapshotCommit {
 public:
  explicit StoreSnapshotCommit(ISnapshotStore& store) : store_(store) {}
  bool commit(LifeSnapshotV2& staged) override {
    return SnapshotStore::commit(store_, staged);
  }

 private:
  ISnapshotStore& store_;
};

}  // namespace xuna
