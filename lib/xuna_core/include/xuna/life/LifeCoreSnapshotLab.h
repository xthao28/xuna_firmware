#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <xuna/LifeEngine.h>
#include <xuna/life/LifeCoreSession.h>
#include <xuna/persist/SnapshotStore.h>

namespace xuna {

// In-memory snapshot store for Life Lab: v1 slots (read-only input for the
// upgrade path) and v2 A/B slots. An armed power cut tears the next write:
// only the first half of the new bytes lands and the write reports failure.
class MemorySnapshotStore : public ISnapshotStore {
 public:
  static constexpr size_t kCapacity = SnapshotCodec::kBudget;

  bool read(SnapshotKey key, uint8_t* out, size_t capacity,
            size_t& size) override {
    const uint8_t k = static_cast<uint8_t>(key);
    if (k >= kKeys || !exists_[k]) return false;
    size = size_[k];
    std::memcpy(out, bytes_[k], size < capacity ? size : capacity);
    return true;
  }

  bool write(SnapshotKey key, const uint8_t* bytes, size_t size) override {
    const uint8_t k = static_cast<uint8_t>(key);
    if (k >= kKeys || size > kCapacity) return false;
    const size_t landed = powerCutNext_ ? size / 2 : size;
    std::memcpy(bytes_[k], bytes, landed);
    size_[k] = size;
    exists_[k] = true;
    if (!powerCutNext_) return true;
    powerCutNext_ = false;
    return false;
  }

  void put(SnapshotKey key, const uint8_t* bytes, size_t size) {
    const uint8_t k = static_cast<uint8_t>(key);
    if (k >= kKeys || size > kCapacity) return;
    std::memcpy(bytes_[k], bytes, size);
    size_[k] = size;
    exists_[k] = true;
  }
  void powerCutNextWrite() { powerCutNext_ = true; }
  void disarm() { powerCutNext_ = false; }
  bool exists(SnapshotKey key) const {
    return exists_[static_cast<uint8_t>(key)];
  }
  // Test-only corruption hook; production adapters never expose raw slots.
  void corrupt(SnapshotKey key) { bytes_[static_cast<uint8_t>(key)][0] ^= 0xFF; }

 private:
  static constexpr uint8_t kKeys = 4;
  uint8_t bytes_[kKeys][kCapacity] = {};
  size_t size_[kKeys] = {};
  bool exists_[kKeys] = {};
  bool powerCutNext_ = false;
};

// Boot/commit harness around LifeCoreSession with LifeSnapshot v2 (S12 codec
// and recovery). It is LifeCore's committer (§9.5) and mirrors the planned
// S13 boot policy: Ok/Migrated/Missing start normally, TooNew, Corrupt and
// MigrationFailed start in ERROR_SAFE (read-only). No NVS/LittleFS.
class LifeCoreSnapshotLab {
 public:
  explicit LifeCoreSnapshotLab(uint64_t seed = 1, uint64_t stream = 3)
      : session_(seed, stream), commit_(store_) {
    session_.core().setCommitter(&commit_);
  }
  // The session holds a pointer to this lab's committer.
  LifeCoreSnapshotLab(const LifeCoreSnapshotLab&) = delete;
  LifeCoreSnapshotLab& operator=(const LifeCoreSnapshotLab&) = delete;

  bool boot(MonoMs now) {
    store_.disarm();
    LifeSnapshotV2 recovered;
    last_ = SnapshotStore::recover(store_, recovered);
    const bool ok = last_.result == LoadResult::Ok ||
                    last_.result == LoadResult::Migrated ||
                    last_.result == LoadResult::Missing;
    session_.core().restore(ok ? recovered : LifeSnapshotV2());
    return session_.begin(now, ok);
  }
  bool reboot(MonoMs now) { return boot(now); }

  // Lazy commit of the current RAM state (plan §7.6, e.g. activeSeconds).
  // Urgent feedback commits go through the committer instead.
  bool persist() {
    LifeSnapshotV2 staged = session_.core().snapshot();
    if (!SnapshotStore::commit(store_, staged)) return false;
    session_.core().snapshot() = staged;
    return true;
  }

  // Seeds a v1 snapshot before the first boot (upgrade path).
  void seedV1(const uint8_t* bytes, size_t size) {
    store_.put(SnapshotKey::V1Slot0, bytes, size);
  }

  void powerCutNextPersist() { store_.powerCutNextWrite(); }
  const RecoveryReport& lastRecovery() const { return last_; }
  MemorySnapshotStore& store() { return store_; }
  LifeCoreSession& session() { return session_; }
  const LifeCoreSession& session() const { return session_; }

 private:
  LifeCoreSession session_;
  MemorySnapshotStore store_;
  StoreSnapshotCommit commit_;
  RecoveryReport last_;
};

}  // namespace xuna
