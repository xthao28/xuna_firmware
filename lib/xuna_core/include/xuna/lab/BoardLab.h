#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include <xuna/LocalBehavior.h>
#include <xuna/base/Random.h>
#include <xuna/life/BootSequence.h>
#include <xuna/life/LifeCoreSnapshotLab.h>
#include <xuna/personality/Expression.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>

namespace xuna {

// In-memory IBootStore: the Life Lab A/B snapshot slots plus the identity
// record and `bootc`. A power cut tears the next snapshot write (half the
// bytes land and the write fails), as in MemorySnapshotStore.
class MemoryBootStore : public IBootStore {
 public:
  bool read(SnapshotKey key, uint8_t* out, size_t capacity,
            size_t& size) override {
    return slots_.read(key, out, capacity, size);
  }
  bool write(SnapshotKey key, const uint8_t* bytes, size_t size) override {
    // The board never writes the v1 rollback keys (NvsSnapshotStore).
    if (key == SnapshotKey::V1Slot0 || key == SnapshotKey::V1Slot1) return false;
    return slots_.write(key, bytes, size);
  }
  IdentityLoad readIdentity(IdentityRecord& out) override {
    if (!hasIdentity_) return IdentityLoad::Missing;
    return out.decode(identity_, sizeof(identity_));
  }
  bool writeIdentity(const IdentityRecord& record) override {
    if (!record.encode(identity_)) return false;
    hasIdentity_ = true;
    return true;
  }
  uint32_t nextBoot() override { return ++bootCounter_; }

  void seedV1(const uint8_t* bytes, size_t size) {
    slots_.put(SnapshotKey::V1Slot0, bytes, size);
  }
  void powerCutNextWrite() { slots_.powerCutNextWrite(); }
  uint32_t bootCounter() const { return bootCounter_; }

 private:
  MemorySnapshotStore slots_;
  uint8_t identity_[IdentityRecord::kEncodedSize] = {};
  bool hasIdentity_ = false;
  uint32_t bootCounter_ = 0;
};

// Host replica of the board: main.cpp's setup() (BootSequence, the same
// adapter flags) and loop() (adapter tick, buttons, offline quiet routine,
// five-minute checkpoint) over an in-memory store. A reboot discards all RAM
// objects and rebuilds them, so only what the store kept survives. No
// display, animation or BLE transport: commands enter as v1 frame bytes.
class BoardLab {
 public:
  explicit BoardLab(uint64_t entropySeed = 7)
      : commit_(store_), entropy_(entropySeed, 11) {}
  BoardLab(const BoardLab&) = delete;
  BoardLab& operator=(const BoardLab&) = delete;

  // setup(): `now` is millis() at boot (the caller restarts it at 0).
  bool boot(uint32_t now) {
    ram_.reset(new Ram(commit_));
    ram_->core.setLeaning(leaning_);
    last_ = BootSequence::run(store_, true, ram_->gated, ram_->core,
                              ram_->identity, entropy_, scratch_, now);
    ram_->adapter.begin(now);
    ram_->local.activity(now);
    return last_.lifeReady;
  }

  // loop() minus display/BLE transport.
  void loop(uint32_t now) {
    ram_->adapter.tick(now);
    if (last_.lifeReady) {
      uint8_t quietAction = 0;
      if (ram_->local.choosePreferred(
              now, ram_->adapter.last().activity == Activity::Idle,
              static_cast<uint8_t>(ram_->adapter.preferred(0)), quietAction,
              Expression::spontaneousWaitMs(ram_->core.personality())))
        ram_->adapter.localSpontaneous(now);
      if (last_.persistReady() && ram_->adapter.checkpointDue(now)) {
        ++checkpoints_;
        if (!ram_->adapter.checkpoint(now)) ++checkpointFailures_;
      }
    }
  }

  // One BLE command write; returns the v1 result code.
  uint8_t command(const uint8_t* bytes, size_t size, uint32_t now) {
    uint32_t id = 0;
    const uint8_t result = ram_->adapter.handle(bytes, size, now,
                                                last_.lifeReady, id);
    ram_->local.activity(now);
    return result;
  }
  bool greeting(uint32_t now) {
    if (!last_.lifeReady) return false;
    ram_->local.activity(now);
    return ram_->adapter.localGreeting(now);
  }
  bool idleButton(uint32_t now) {
    if (!last_.lifeReady) return false;
    ram_->local.activity(now);
    return ram_->adapter.localIdle(now);
  }
  // Power fails while the next snapshot write is in flight; the caller then
  // reboots. Returns the (expected false) checkpoint result.
  bool powerCutDuringCheckpoint(uint32_t now) {
    store_.powerCutNextWrite();
    return ram_->adapter.checkpoint(now);
  }

  // v1 frames (BleProtocol.h).
  static size_t suggest(uint8_t* b, uint32_t id, uint8_t context) {
    return frame(b, 2, id, context, 0, 0);
  }
  static size_t feedback(uint8_t* b, uint32_t id, uint32_t target,
                         bool approved, uint8_t source = 1) {
    return frame(b, 3, id, approved ? 1 : 0, source, target);
  }
  static size_t forget(uint8_t* b, uint32_t id) { return frame(b, 4, id, 0, 0, 0); }
  static size_t timeSync(uint8_t* b, uint32_t id, uint32_t utcSeconds,
                         int16_t tzOffsetMin) {
    std::memset(b, 0, 16);
    b[0] = 1;
    b[1] = 5;
    LifeEngine::put32(b + 2, id);
    b[6] = 1;  // source: phone
    LifeEngine::put32(b + 8, utcSeconds);
    b[12] = uint8_t(uint16_t(tzOffsetMin));
    b[13] = uint8_t(uint16_t(tzOffsetMin) >> 8);
    return 16;
  }

  // PE-4a gains for this and every later boot (lab only; the board keeps
  // the LifeCore defaults).
  void setLeaning(const LeaningConfig& config) {
    leaning_ = config;
    if (ram_) ram_->core.setLeaning(config);
  }

  const BootResult& lastBoot() const { return last_; }
  const LifeSnapshotV2& snapshot() const { return ram_->core.snapshot(); }
  const LifeCore& core() const { return ram_->core; }
  const V1LifeCoreAdapter& adapter() const { return ram_->adapter; }
  const IdentityService& identity() const { return ram_->identity; }
  MemoryBootStore& store() { return store_; }
  uint32_t checkpoints() const { return checkpoints_; }
  uint32_t checkpointFailures() const { return checkpointFailures_; }

 private:
  static size_t frame(uint8_t* b, uint8_t opcode, uint32_t id, uint8_t arg,
                      uint8_t source, uint32_t target) {
    std::memset(b, 0, 12);
    b[0] = 1;
    b[1] = opcode;
    LifeEngine::put32(b + 2, id);
    b[6] = arg;
    b[7] = source;
    if (opcode != 3) return 8;
    LifeEngine::put32(b + 8, target);
    return 12;
  }

  // Everything main.cpp keeps in RAM (globals), rebuilt on every boot.
  struct Ram {
    explicit Ram(ISnapshotCommit& commit)
        : gated(commit, identity), adapter(core, gated, true, true) {}
    IdentityService identity;
    IdentityGatedCommit gated;
    LifeCore core;
    V1LifeCoreAdapter adapter;
    LocalBehavior local;
  };

  MemoryBootStore store_;
  StoreSnapshotCommit commit_;
  Pcg32 entropy_;  // lab only; the board uses esp_random()
  LifeSnapshotV2 scratch_;
  std::unique_ptr<Ram> ram_;
  LeaningConfig leaning_;
  BootResult last_;
  uint32_t checkpoints_ = 0;
  uint32_t checkpointFailures_ = 0;
};

}  // namespace xuna
