// S20 golden vectors: BLE v1 command frames with the real adapter's results
// and the life / memory values the app reads after each command.
//
//   c++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/golden.cpp -o golden
//   ./golden > sim/golden/learning_v1.json
//
// The device is BoardLab (main.cpp setup()/loop() replica) booted from a blank
// v1 snapshot, i.e. a migrated pet without history, so it can learn (an
// unhatched device answers not_ready). Deterministic: rerun and diff.
#include <cstdio>
#include <cstring>

#include <xuna/LifeEngine.h>
#include <xuna/lab/BoardLab.h>

namespace {

using xuna::BoardLab;

struct Step {
  const char* op;
  uint8_t opcode;
  uint8_t arg;
  uint8_t source;
  uint32_t target;  // feedback: rated request ID; stated: action << 8 | contextKey
  uint32_t gapMs;   // device time before this command
};

// Request IDs are 1, 2, 3… in order, as the app's first session sends them.
const Step kSteps[] = {
    {"suggest", 2, 0, 0, 0, 1000},          // 1 quiet
    {"feedback", 3, 1, 1, 1, 5000},         // 2 approve 1 (button)
    {"suggest", 2, 1, 0, 0, 30000},         // 3 play
    {"feedback", 3, 0, 2, 3, 5000},         // 4 reject 3 (confirmed text)
    {"feedback", 3, 1, 1, 3, 2000},         // 5 rating 3 again: consumed
    {"feedback", 3, 1, 1, 999, 2000},       // 6 unknown target
    {"suggest", 2, 1, 0, 0, 30000},         // 7 play
    {"animation", 1, 1, 0, 0, 2000},        // 8 direct happy: invalidates 7
    {"feedback", 3, 1, 1, 7, 2000},         // 9 rating 7 after direct input
    {"suggest", 2, 1, 0, 0, 30000},         // 10
    {"feedback", 3, 1, 1, 10, 5000},        // 11
    {"suggest", 2, 1, 0, 0, 30000},         // 12
    {"feedback", 3, 0, 1, 12, 5000},        // 13
    {"suggest", 2, 1, 0, 0, 30000},         // 14
    {"feedback", 3, 1, 1, 14, 5000},        // 15
    {"suggest", 2, 1, 0, 0, 30000},         // 16
    {"feedback", 3, 0, 1, 16, 5000},        // 17
    {"suggest", 2, 1, 0, 0, 30000},         // 18
    {"feedback", 3, 1, 1, 18, 5000},        // 19
    {"suggest", 2, 0, 0, 0, 30000},         // 20 quiet
    {"feedback", 3, 1, 2, 20, 5000},        // 21
    {"stated", 6, 1, 2, (3u << 8) | 0x10, 30000},  // 22 "likes curious when playing"
    {"suggest", 2, 1, 0, 0, 30000},         // 23
    {"feedback", 3, 1, 1, 23, 5000},        // 24
    {"forget", 4, 0, 0, 0, 30000},          // 25
    {"suggest", 2, 1, 0, 0, 30000},         // 26
    {"feedback", 3, 1, 1, 26, 5000},        // 27
};

size_t frame(uint8_t* b, const Step& s, uint32_t id) {
  std::memset(b, 0, 16);
  b[0] = 1;
  b[1] = s.opcode;
  xuna::LifeEngine::put32(b + 2, id);
  b[6] = s.arg;
  b[7] = s.source;
  if (s.opcode == 3) {
    xuna::LifeEngine::put32(b + 8, s.target);
    return 12;
  }
  if (s.opcode == 6) {
    b[8] = uint8_t(s.target);
    b[9] = uint8_t(s.target >> 8);
    return 12;
  }
  return 8;
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
  std::printf(
      "{\n  \"schema\": \"xuna.golden.v1\",\n"
      "  \"generator\": \"firmware/sim/golden.cpp\",\n"
      "  \"device\": \"BoardLab: V1LifeCoreAdapter(enhanced), LifeCore v3, migrated blank v1 pet\",\n"
      "  \"simulatorSkips\": {\"learner\": [\"life.actionCode\", \"life.quietChoiceCode\", "
      "\"life.playChoiceCode\", \"memory.actionCode\"], \"clock\": [\"life.activeSeconds\"]},\n"
      "  \"steps\": [\n");
  uint32_t now = 0;
  const size_t count = sizeof(kSteps) / sizeof(kSteps[0]);
  for (size_t i = 0; i < count; ++i) {
    const Step& s = kSteps[i];
    for (uint32_t t = now + 1000; t < now + s.gapMs; t += 1000) lab.loop(t);
    now += s.gapMs;
    lab.loop(now);
    uint8_t bytes[16];
    const size_t size = frame(bytes, s, uint32_t(i + 1));
    const uint8_t result = lab.command(bytes, size, now);
    uint8_t life[20], page0[20], page1[20];
    lab.adapter().encodeLife(life);
    lab.adapter().encodeMemory(0, page0);
    lab.adapter().encodeMemory(1, page1);
    std::printf("    {\"requestId\": %u, \"op\": \"%s\", \"opcode\": %u, \"argument\": %u, "
                "\"source\": %u, \"target\": %u, \"atMs\": %u,\n     \"frame\": ",
                unsigned(i + 1), s.op, s.opcode, s.arg, s.source, unsigned(s.target),
                unsigned(now));
    hex(bytes, size);
    std::printf(", \"result\": %u,\n     \"life\": ", result);
    hex(life, sizeof(life));
    std::printf(",\n     \"memory0\": ");
    hex(page0, sizeof(page0));
    std::printf(", \"memory1\": ");
    hex(page1, sizeof(page1));
    std::printf("}%s\n", i + 1 < count ? "," : "");
  }
  std::printf("  ]\n}\n");
  return 0;
}
