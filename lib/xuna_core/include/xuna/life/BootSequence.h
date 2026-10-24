#pragma once

#include <cstdint>
#include <cstring>

#include <xuna/base/Random.h>
#include <xuna/identity/IdentityService.h>
#include <xuna/life/LifeCore.h>
#include <xuna/persist/SnapshotStore.h>

namespace xuna {

// Boot-time storage port: LifeSnapshot slots plus the identity record and the
// boot counter `bootc` (plan §7.1). NvsSnapshotStore on the board, an
// in-memory store in Life Lab.
class IBootStore : public ISnapshotStore {
 public:
  virtual IdentityLoad readIdentity(IdentityRecord& out) = 0;
  virtual bool writeIdentity(const IdentityRecord& record) = 0;
  // Increments and commits `bootc`; 0 = the counter could not be committed.
  virtual uint32_t nextBoot() = 0;
};

// Commit port that refuses every snapshot write until the pet has an
// identity. Keeps the S13/S18 invariant "a durable v2 snapshot implies a
// committed identity", so "snapshot Ok + identity Missing" at boot still means
// damage. An unhatched (Egg) device therefore lives but does not learn: its
// ratings answer not_ready until it hatches (S10-hatch).
class IdentityGatedCommit : public ISnapshotCommit {
 public:
  IdentityGatedCommit(ISnapshotCommit& inner, const IdentityService& identity)
      : inner_(inner), identity_(identity) {}
  bool commit(LifeSnapshotV2& staged) override {
    return identity_.state() == IdentityState::Ready && inner_.commit(staged);
  }

 private:
  ISnapshotCommit& inner_;
  const IdentityService& identity_;
};

struct BootResult {
  RecoveryReport recovery;
  uint32_t boot = 0;
  bool lifeReady = false;
  // Lazy checkpoints only with an identity (see IdentityGatedCommit).
  bool persistReady() const { return lifeReady && identityReady; }
  bool identityReady = false;
};

// S13 + S18 boot policy, shared by main.cpp setup() and Life Lab so host
// scenarios replay the board's exact order:
//   1. commit the next boot counter, recover the newest valid snapshot;
//   2. Ok/Migrated/Missing start normally; TooNew/Corrupt/MigrationFailed or
//      a failed counter start read-only (ERROR_SAFE);
//   3. cold-boot needs: a recovered need state is never resumed;
//   4. identity: a migrated v1 pet gets a new PetId, committed before input;
//      a v2 snapshot without its identity record stays read-only;
//   5. a migrated snapshot is committed as v2 before input is enabled;
//   6. LifeCore::begin. The caller then starts its adapter. Pass an
//      IdentityGatedCommit as `commit` (and to the adapter).
// `scratch` holds the recovered snapshot (several KB; the board keeps it
// static to spare the loop stack). `entropy` is used only for a migration.
class BootSequence {
 public:
  static BootResult run(IBootStore& store, bool storageReady,
                        ISnapshotCommit& commit, LifeCore& core,
                        IdentityService& identity, IRandom& entropy,
                        LifeSnapshotV2& scratch, uint32_t now) {
    BootResult r;
    r.boot = storageReady ? store.nextBoot() : 0;
    r.recovery = SnapshotStore::recover(store, scratch);
    const LoadResult loaded = r.recovery.result;
    r.lifeReady = storageReady && r.boot &&
                  (loaded == LoadResult::Ok || loaded == LoadResult::Migrated ||
                   loaded == LoadResult::Missing);
    scratch.counters.bootCounter = r.boot;
    scratch.hasNeeds = false;
    if (loaded == LoadResult::Ok || loaded == LoadResult::Migrated)
      core.restore(scratch);

    IdentityRecord record;
    const IdentityLoad identityLoad = store.readIdentity(record);
    const bool migratedPet = loaded == LoadResult::Migrated;
    identity.onBoot(r.boot, identityLoad, &record, migratedPet);
    if (r.lifeReady && identity.state() == IdentityState::NeedsMigration) {
      IdentityEntropy fresh;
      for (size_t i = 0; i < sizeof(fresh.petId); i += 4) {
        const uint32_t word = entropy.next();
        std::memcpy(fresh.petId + i, &word, 4);
      }
      fresh.seed = entropy.next();
      r.lifeReady = identity.prepareMigration(
                        fresh, scratch.counters.activeSeconds, now, record) &&
                    store.writeIdentity(record) &&
                    identity.acceptPersisted(record);
    }
    if (identity.state() == IdentityState::Safe ||
        (loaded == LoadResult::Ok && identityLoad == IdentityLoad::Missing))
      r.lifeReady = false;
    // Persist the migration before enabling input; v1 keys stay read-only.
    if (r.lifeReady && migratedPet) {
      r.lifeReady = commit.commit(scratch);
      if (r.lifeReady) core.restore(scratch);
    }
    r.identityReady = identity.state() == IdentityState::Ready;
    core.seedPersonality(r.identityReady ? identity.record().seed : 0);
    core.snapshot().counters.bootCounter = r.boot;
    core.begin(now, r.lifeReady);
    return r;
  }
};

}  // namespace xuna
