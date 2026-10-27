// KD-2 golden vectors: BLE v1 opcode 10 knowledge command frames with
// real V1LifeCoreAdapter results and the 64-byte projection read after each command.
//
// Build and run:
//   g++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/knowledge_golden.cpp -o sim/knowledge_golden
//   ./sim/knowledge_golden > ../mobile/test/fixtures/firmware/knowledge_v1.json

#include <cstdio>
#include <cstring>
#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/lab/BoardLab.h>
#include <xuna/knowledge/KnowledgeProjection.h>

namespace {

using xuna::BoardLab;

struct KnowledgeStep {
  const char* op;
  uint8_t subOp;
  uint8_t slot;
  uint8_t slot2;
  uint32_t factKey;
  uint32_t gapMs;
};

const KnowledgeStep kSteps[] = {
    {"open_slot_0", 1, 0, 0, 0, 1000},
    {"open_slot_1", 1, 1, 0, 0, 2000},
    {"add_fact_1", 5, 0, 0, 0x00010001, 2000},
    {"add_fact_2", 5, 0, 0, 0x00010002, 2000},
    {"add_fact_3", 5, 0, 0, 0x00010003, 2000},
    {"add_duplicate_fact_1", 5, 0, 0, 0x00010001, 2000},
    {"move_fact_2_to_slot_1", 7, 0, 1, 0x00010002, 2000},
    {"set_resting_slot_1", 3, 1, 0, 0, 2000},
    {"set_active_slot_1", 2, 1, 0, 0, 2000},
    {"remove_fact_1", 6, 0, 0, 0x00010001, 2000},
    {"clear_slot_0", 4, 0, 0, 0, 2000},
};

size_t frame(uint8_t* b, uint32_t id, const KnowledgeStep& s) {
  std::memset(b, 0, 16);
  b[0] = 1;
  b[1] = 10;
  xuna::LifeEngine::put32(b + 2, id);
  b[6] = s.subOp;
  b[7] = 0;  // source 0
  b[8] = s.slot;
  b[9] = s.slot2;
  xuna::LifeEngine::put32(b + 12, s.factKey);
  return 16;
}

void hex(const uint8_t* b, size_t n) {
  std::printf("\"");
  for (size_t i = 0; i < n; ++i) std::printf("%02x", b[i]);
  std::printf("\"");
}

}  // namespace

int main() {
  uint8_t v1[xuna::LifeEngine::kSnapshotSize];
  xuna::LifeEngine blank;
  blank.encode(v1);
  BoardLab lab;
  lab.store().seedV1(v1, sizeof(v1));
  if (!lab.boot(0)) {
    std::fprintf(stderr, "boot not ready\n");
    return 1;
  }

  // Initial TimeSync so clock is trusted (2026-10-27 10:00:00 UTC, tz +7h = 420 min)
  uint8_t syncFrame[16];
  BoardLab::timeSync(syncFrame, 100, 1793104800U, 420);
  lab.command(syncFrame, sizeof(syncFrame), 500);

  std::printf(
      "{\n  \"schema\": \"xuna.knowledge.golden.v1\",\n"
      "  \"generator\": \"firmware/sim/knowledge_golden.cpp\",\n"
      "  \"device\": \"BoardLab: V1LifeCoreAdapter(enhanced), LifeCore, section 13 knowledge\",\n"
      "  \"steps\": [\n");

  uint32_t now = 1000;
  const size_t count = sizeof(kSteps) / sizeof(kSteps[0]);
  for (size_t i = 0; i < count; ++i) {
    const KnowledgeStep& s = kSteps[i];
    now += s.gapMs;
    lab.loop(now);

    uint8_t bytes[16];
    const size_t size = frame(bytes, uint32_t(i + 1), s);
    const unsigned result = lab.command(bytes, size, now);

    uint8_t proj[xuna::KnowledgeProjection::kSize];
    lab.adapter().encodeKnowledge(proj);

    std::printf(
        "    {\"requestId\": %u, \"op\": \"%s\", \"opcode\": 10, \"subOp\": %u, "
        "\"slot\": %u, \"slot2\": %u, \"factKey\": %u, \"atMs\": %u,\n     \"frame\": ",
        unsigned(i + 1), s.op, s.subOp, s.slot, s.slot2, unsigned(s.factKey), unsigned(now));
    hex(bytes, size);
    std::printf(", \"result\": %u,\n     \"projection\": ", result);
    hex(proj, sizeof(proj));
    std::printf("}%s\n", i + 1 < count ? "," : "");
  }

  std::printf("  ]\n}\n");
  return 0;
}
