#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/learning/LearnerV3Codec.h>
#include <xuna/persist/SnapshotCodec.h>

namespace xuna {

// v1 (96-byte LifeEngine snapshot) → LifeSnapshot v2 (S12b, plan §7.5).
// Pure and idempotent; nothing is written here. Mapping:
//   counts   → coarse learner cells (day part unknown) with the E10-C cap:
//              ratio kept, mass ≤ 1/(1 − γ), raw count in `rated`
//   episodes → Episodes section (same ring order), consumed by M05b later
//   activeSeconds, generation → same fields; eventCount → feedbackCount
// bootCounter stays 0 (the adapter copies key `bootc`); needs are absent so
// the caller applies its cold-boot needs policy.
inline bool migrateV1(const LifeEngine& v1, LifeSnapshotV2& out) {
  LifeSnapshotV2 candidate(out.learner.config());
  if (!LearnerV3Codec::migrateFromV1(v1, candidate.learner)) return false;
  candidate.generation = v1.generation;
  candidate.counters.activeSeconds = v1.activeSeconds;
  candidate.counters.feedbackCount = v1.eventCount;
  candidate.episodes.count = v1.episodeCount;
  candidate.episodes.next = v1.episodeNext;
  for (uint8_t i = 0; i < SnapshotEpisodes::kCapacity; ++i)
    candidate.episodes.items[i] = v1.episodes[i];
  out = candidate;
  return true;
}

// Decodes one v1 snapshot and migrates it. `out` is unchanged on failure.
inline LoadResult fromV1(const uint8_t* bytes, size_t size, LifeSnapshotV2& out) {
  LifeEngine v1;
  if (!v1.decode(bytes, size)) return LoadResult::MigrationFailed;
  return migrateV1(v1, out) ? LoadResult::Migrated : LoadResult::MigrationFailed;
}

}  // namespace xuna
