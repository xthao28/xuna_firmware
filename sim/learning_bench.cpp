// Closed-loop learning benchmark (Life Lab Phase 0, plan §9.9 / E10).
// Host-only tool: uses std::vector and doubles for statistics; the simulated
// owner and learners come from the fixed-point core headers.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <xuna/lab/LearnerBench.h>

namespace {

using namespace xuna;

struct Scenario {
  const char* name;
  OwnerConfig owner;
  BenchConfig bench;
};

OwnerConfig base(uint8_t actions = 2) {
  OwnerConfig config;
  config.contexts = 2;
  config.actions = actions;
  config.preferred[0] = 1;
  config.preferred[1] = 0;
  config.laterPreferred[0] = 0;
  config.laterPreferred[1] = 1;
  return config;
}

BenchConfig trials(uint32_t count) {
  BenchConfig config;
  config.trials = count;
  return config;
}

std::vector<Scenario> scenarios() {
  std::vector<Scenario> list;
  {
    Scenario s = {"B_oracle", base(), trials(60)};
    list.push_back(s);
  }
  {
    Scenario s = {"C_reversal", base(), trials(120)};
    s.owner.kind = OwnerKind::Reversal;
    s.owner.changeAt = 60;
    list.push_back(s);
  }
  {
    Scenario s = {"D_no_feedback", base(), trials(100)};
    s.owner.kind = OwnerKind::NoFeedback;
    list.push_back(s);
  }
  {
    Scenario s = {"drift", base(), trials(160)};
    s.owner.kind = OwnerKind::Drift;
    s.owner.changeAt = 20;
    s.owner.driftLength = 100;
    list.push_back(s);
  }
  {
    Scenario s = {"noisy", base(), trials(120)};
    s.owner.preferredQ16 = 39322;  // 0.6
    s.owner.otherQ16 = 26214;      // 0.4
    list.push_back(s);
  }
  {
    Scenario s = {"sparse", base(), trials(300)};
    s.owner.rateQ16 = 6554;  // about 10% of shown actions are rated
    list.push_back(s);
  }
  {
    Scenario s = {"B_oracle_4actions", base(4), trials(120)};
    s.owner.preferred[0] = 3;
    s.owner.preferred[1] = 2;
    list.push_back(s);
  }
  return list;
}

struct Interval {
  double mean;
  double low;
  double high;
};

// Percentile bootstrap of the mean, deterministic resampling.
Interval bootstrap(const std::vector<double>& values) {
  Interval result = {0, 0, 0};
  if (values.empty()) return result;
  double sum = 0;
  for (double v : values) sum += v;
  result.mean = sum / values.size();
  Pcg32 random(2026, 15);
  const int kResamples = 1000;
  std::vector<double> means;
  means.reserve(kResamples);
  for (int r = 0; r < kResamples; ++r) {
    double s = 0;
    for (size_t i = 0; i < values.size(); ++i)
      s += values[random.below(static_cast<uint32_t>(values.size()))];
    means.push_back(s / values.size());
  }
  std::sort(means.begin(), means.end());
  result.low = means[static_cast<size_t>(0.025 * kResamples)];
  result.high = means[static_cast<size_t>(0.975 * kResamples) - 1];
  return result;
}

double percentile(std::vector<double> values, double p) {
  if (values.empty()) return NAN;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>(p * (values.size() - 1) + 0.5);
  return values[index];
}

struct LearnerEntry {
  const char* name;
  IBenchLearner* learner;
};

void usage() {
  std::fprintf(stderr,
               "usage: learning_bench [--seeds N] [--curves]\n"
               "  default: summary CSV for every scenario and learner\n"
               "  --curves: per-trial greedy-correct rate instead\n");
}

}  // namespace

int main(int argc, char** argv) {
  uint32_t seeds = 200;
  bool curves = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--curves") == 0) {
      curves = true;
    } else if (std::strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
      seeds = static_cast<uint32_t>(std::strtoul(argv[++i], 0, 10));
      if (seeds == 0) seeds = 1;
    } else {
      usage();
      return 2;
    }
  }

  RandomBenchLearner random;
  StaticBenchLearner fixed;
  V1BenchLearner v1;
  V2BenchLearner v2;
  const LearnerEntry learners[] = {
      {"random", &random}, {"static", &fixed}, {"v1", &v1}, {"v2", &v2}};

  if (curves) {
    std::printf("scenario,learner,trial,greedy_correct_rate\n");
  } else {
    std::printf(
        "scenario,learner,seeds,trials,regret_mean,regret_ci_low,"
        "regret_ci_high,last20_greedy_mean,last20_greedy_ci_low,"
        "last20_greedy_ci_high,target_reached,target_median,target_p90,"
        "flips_after_target_mean,rated_mean,brier_per_rated,"
        "baseline_brier_per_rated,state_unchanged_all\n");
  }

  const std::vector<Scenario> list = scenarios();
  for (const Scenario& scenario : list) {
    const OwnerModel owner(scenario.owner);
    const uint32_t count = scenario.bench.trials;
    for (const LearnerEntry& entry : learners) {
      if (!entry.learner->supports(scenario.owner.contexts,
                                   scenario.owner.actions)) {
        if (!curves)
          std::printf("%s,%s,0,%u,unsupported,,,,,,,,,,,,,\n", scenario.name,
                      entry.name, count);
        continue;
      }
      std::vector<double> regret, last, target, flips, rated;
      std::vector<double> curve(count, 0.0);
      std::vector<uint8_t> trace(count, 0);
      double brier = 0, baseline = 0, ratedTotal = 0;
      bool unchanged = true;
      for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const BenchResult r = LearnerBench::run(
            *entry.learner, owner, scenario.bench, seed, trace.data());
        regret.push_back(r.regretQ16 / 65536.0);
        last.push_back(static_cast<double>(r.lastWindowGreedy) /
                       scenario.bench.lastWindow);
        if (r.trialsToTarget != LearnerBench::kNever) {
          target.push_back(r.trialsToTarget);
          flips.push_back(r.flipsAfterTarget);
        }
        rated.push_back(r.rated);
        ratedTotal += r.rated;
        brier += r.brierQ32 / 4294967296.0;
        baseline += r.baselineBrierQ32 / 4294967296.0;
        unchanged = unchanged && r.stateUnchanged;
        for (uint32_t t = 0; t < count; ++t) curve[t] += trace[t];
      }

      if (curves) {
        for (uint32_t t = 0; t < count; ++t)
          std::printf("%s,%s,%u,%.4f\n", scenario.name, entry.name, t,
                      curve[t] / seeds);
        continue;
      }
      const Interval reg = bootstrap(regret);
      const Interval win = bootstrap(last);
      double flipMean = 0;
      for (double f : flips) flipMean += f;
      if (!flips.empty()) flipMean /= flips.size();
      double ratedMean = 0;
      for (double r : rated) ratedMean += r;
      ratedMean /= rated.size();
      std::printf(
          "%s,%s,%u,%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.2f,"
          "%.1f,%.4f,%.4f,%s\n",
          scenario.name, entry.name, seeds, count, reg.mean, reg.low,
          reg.high, win.mean, win.low, win.high,
          static_cast<double>(target.size()) / seeds,
          percentile(target, 0.5), percentile(target, 0.9), flipMean,
          ratedMean, ratedTotal > 0 ? brier / ratedTotal : NAN,
          ratedTotal > 0 ? baseline / ratedTotal : NAN,
          unchanged ? "yes" : "no");
    }
  }
  return 0;
}
