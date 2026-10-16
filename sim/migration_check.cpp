// Host check for the S13 + S18 switch (session plan §9 item 1): load the
// owner's real v1 snapshots (extracted read-only from an NVS backup with
// tools/nvs_life_dump.py), then compare what the board shows today
// (v1 LifeEngine + BoardLearner) with what the LifeCore path will show after
// the v1 → v2 migration. Exit code 0 = preferences and every v1 life/memory
// byte that does not depend on the session agree.
//
//   c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include \
//       sim/migration_check.cpp -o migration_check
//   ./migration_check slot0.bin slot1.bin
#include <cstdio>
#include <cstring>

#include <xuna/learning/BoardLearner.h>
#include <xuna/life/LifeCoreSnapshotLab.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>

using namespace xuna;

namespace {

size_t load(const char* path, uint8_t* out, size_t capacity) {
  std::FILE* file = std::fopen(path, "rb");
  if (!file) return 0;
  const size_t n = std::fread(out, 1, capacity, file);
  std::fclose(file);
  return n;
}

void printCell(const PreferenceLearnerV3& learner, uint8_t interaction,
               uint8_t action) {
  const auto view = learner.view(interaction, 0, action);
  std::printf(" p=%.3f", view.pQ16 / 65536.0);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s slot0.bin slot1.bin\n", argv[0]);
    return 2;
  }
  uint8_t v1[2][LifeEngine::kSnapshotSize] = {};
  size_t size[2];
  MemorySnapshotStore store;
  for (int i = 0; i < 2; ++i) {
    size[i] = load(argv[1 + i], v1[i], sizeof(v1[i]));
    if (size[i])
      store.put(i ? SnapshotKey::V1Slot1 : SnapshotKey::V1Slot0, v1[i], size[i]);
  }

  // Today: v1 durable record + BoardLearner rebuilt at boot.
  LifeEngine life;
  if (!life.recover(v1[0], size[0], v1[1], size[1])) {
    std::printf("v1 recover FAILED\n");
    return 1;
  }
  BoardLearner board(1);
  board.restore(life);
  std::printf("v1: generation=%u active_s=%u feedback=%u episodes=%u\n",
              life.generation, life.activeSeconds, life.eventCount,
              life.episodeCount);
  for (int c = 0; c < 2; ++c)
    std::printf("v1 counts ctx%d: idle +%u/-%u happy +%u/-%u\n", c,
                life.positive[c][0], life.negative[c][0], life.positive[c][1],
                life.negative[c][1]);

  // After the switch: recover (migration) → LifeCore → v1 adapter.
  LifeSnapshotV2 snapshot;
  const RecoveryReport report = SnapshotStore::recover(store, snapshot);
  if (report.result != LoadResult::Migrated) {
    std::printf("v2 recover result=%u (expected Migrated)\n",
                unsigned(report.result));
    return 1;
  }
  StoreSnapshotCommit commit(store);
  LifeCore core(1, 3);
  core.restore(snapshot);
  core.begin(0, true);
  V1LifeCoreAdapter adapter(core, commit);
  adapter.begin(0);

  int failures = 0;
  for (uint8_t c = 0; c < 2; ++c) {
    const uint8_t before = board.preferred(c);  // learner index = ActionId 0/1
    const uint8_t after = static_cast<uint8_t>(adapter.preferred(c));
    std::printf("context %u: board=%u lifecore=%u", c, before, after);
    printCell(core.learner(), c, 0);
    printCell(core.learner(), c, 1);
    printCell(core.learner(), c, 2);
    std::printf("%s\n", before == after ? "" : "  MISMATCH");
    if (before != after) ++failures;
  }

  // v1 bytes the app reads, excluding session-only fields (shown context/
  // action, decision ID, canFeedback), which reset on every boot anyway.
  uint8_t got[20];
  adapter.encodeLife(got);
  if (LifeEngine::get32(got + 4) != life.activeSeconds ||
      LifeEngine::get32(got + 8) != life.eventCount ||
      got[19] != life.episodeCount) {
    std::printf("life counters MISMATCH\n");
    ++failures;
  }
  for (uint8_t page = 0; page < 2; ++page) {
    uint8_t want[20] = {1, 0, page, 0};
    for (int i = 0; i < 4; ++i) {
      const int index = page * 4 + i;
      if (index >= life.episodeCount) break;
      const int oldest = life.episodeCount == 8 ? life.episodeNext : 0;
      const auto& e = life.episodes[(oldest + index) % 8];
      want[4 + i * 4] = e.context;
      want[5 + i * 4] = e.action;
      want[6 + i * 4] = e.approved;
      want[7 + i * 4] = e.source;
      ++want[1];
    }
    adapter.encodeMemory(page, got);
    if (std::memcmp(want, got, 20)) {
      std::printf("memory page %u MISMATCH\n", page);
      ++failures;
    }
  }
  std::printf("result: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
