#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <xuna/life/LifeCoreSession.h>

namespace xuna {

// In-memory A/B persistence harness for Life Lab. It mirrors the existing
// LifeEngine codec and recovery contract without touching NVS/LittleFS.
class LifeCoreSnapshotLab {
 public:
  static constexpr uint8_t kSlotCount = 2;

  explicit LifeCoreSnapshotLab(uint64_t seed = 1, uint64_t stream = 3)
      : session_(seed, stream) {}

  bool boot(MonoMs now) {
    powerCutNext_ = false;
    LifeEngine restored;
    const bool recovered = recover(restored);
    session_.core().life() = LifeEngine();
    if (!session_.begin(now, true)) return false;
    if (recovered) session_.core().life() = restored;
    return true;
  }

  bool persist() {
    LifeEngine snapshot = session_.core().life();
    ++snapshot.generation;
    uint8_t target = 0;
    if (valid_[0] && valid_[1]) {
      target = generation_[0] <= generation_[1] ? 0 : 1;
    } else if (valid_[0]) {
      target = 1;
    }
    if (powerCutNext_) {
      powerCutNext_ = false;
      return false;
    }
    snapshot.encode(bytes_[target]);
    valid_[target] = true;
    generation_[target] = snapshot.generation;
    session_.core().life() = snapshot;
    return true;
  }

  void powerCutNextPersist() { powerCutNext_ = true; }
  bool reboot(MonoMs now) { return boot(now); }

  LifeCoreSession& session() { return session_; }
  const LifeCoreSession& session() const { return session_; }
  bool slotValid(uint8_t slot) const {
    return slot < kSlotCount && valid_[slot];
  }
  uint32_t slotGeneration(uint8_t slot) const {
    return slot < kSlotCount ? generation_[slot] : 0;
  }

  // Test-only corruption hook; production adapters never expose raw slots.
  void corruptSlot(uint8_t slot) {
    if (slot >= kSlotCount) return;
    bytes_[slot][0] ^= 0xff;
  }

 private:
  bool recover(LifeEngine& output) const {
    const size_t firstSize = valid_[0] ? LifeEngine::kSnapshotSize : 0;
    const size_t secondSize = valid_[1] ? LifeEngine::kSnapshotSize : 0;
    return output.recover(bytes_[0], firstSize, bytes_[1], secondSize);
  }

  LifeCoreSession session_;
  uint8_t bytes_[kSlotCount][LifeEngine::kSnapshotSize] = {};
  uint32_t generation_[kSlotCount] = {};
  bool valid_[kSlotCount] = {};
  bool powerCutNext_ = false;
};

}  // namespace xuna
