// S19b Life Lab scenarios with runtimes. Host only:
//   c++ -std=c++11 -O2 -Wall -Wextra -Werror -Ilib/xuna_core/include sim/scenarios.cpp -o scenarios
#include <chrono>
#include <cstdio>

#include <xuna/lab/LifeScenarios.h>

#include "../test/fixtures/v1_snapshots.h"

namespace {

bool print(const char* name, const xuna::ScenarioReport& r, double seconds) {
  std::printf(
      "%-9s %s steps=%llu boots=%u cuts=%u suggestions=%u acked=%u "
      "feedback=%u duplicates_rejected=%u checkpoints=%u syncs=%u "
      "active_s=%u max_age_loss_s=%u runtime_s=%.2f%s%s\n",
      name, r.passed ? "PASS" : "FAIL", static_cast<unsigned long long>(r.steps),
      r.boots, r.powerCuts, r.suggestions, r.feedbackAcked, r.feedbackCount,
      r.duplicatesRejected, r.checkpoints, r.syncsAccepted, r.activeSeconds,
      r.maxAgeLossSeconds, seconds, r.passed ? "" : " reason=", r.failure);
  return r.passed;
}

template <typename F>
bool timed(const char* name, F run) {
  const auto start = std::chrono::steady_clock::now();
  const xuna::ScenarioReport r = run();
  const std::chrono::duration<double> spent = std::chrono::steady_clock::now() - start;
  return print(name, r, spent.count());
}

}  // namespace

int main() {
  bool ok = timed("D", [] {
    return xuna::LifeScenarios::noFeedback(xuna_fixture::scripted,
                                           xuna::LifeEngine::kSnapshotSize, 100);
  });
  ok = timed("E", [] {
         return xuna::LifeScenarios::rebootsAndPowerCuts(
             xuna_fixture::scripted, xuna::LifeEngine::kSnapshotSize);
       }) && ok;
  ok = timed("stability", [] { return xuna::LifeScenarios::stability(
        xuna_fixture::scripted, xuna::LifeEngine::kSnapshotSize, 100); }) && ok;
  return ok ? 0 : 1;
}
