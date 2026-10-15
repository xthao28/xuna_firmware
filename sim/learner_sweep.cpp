// Parameter sweep for learner v3 (Life Lab, plan §9.3/§9.4, E10).
// Host-only tool. One CSV row per configuration with mean regret per scenario,
// noisy-owner flips and late-context generalization.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

OwnerConfig base(uint8_t contexts = 2, uint8_t actions = 2) {
  OwnerConfig config;
  config.contexts = contexts;
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
  Scenario s = {"B_oracle", base(), trials(60)};
  list.push_back(s);

  s = Scenario{"C_reversal", base(), trials(120)};
  s.owner.kind = OwnerKind::Reversal;
  s.owner.changeAt = 60;
  list.push_back(s);

  s = Scenario{"drift", base(), trials(160)};
  s.owner.kind = OwnerKind::Drift;
  s.owner.changeAt = 20;
  s.owner.driftLength = 100;
  list.push_back(s);

  s = Scenario{"noisy", base(), trials(120)};
  s.owner.preferredQ16 = 39322;
  s.owner.otherQ16 = 26214;
  list.push_back(s);

  s = Scenario{"sparse", base(), trials(300)};
  s.owner.rateQ16 = 6554;
  list.push_back(s);

  s = Scenario{"B_4actions", base(2, 4), trials(120)};
  s.owner.preferred[0] = 3;
  s.owner.preferred[1] = 2;
  list.push_back(s);

  // Day parts disagree inside interaction 1.
  s = Scenario{"dayparts", base(4), trials(160)};
  s.owner.preferred[0] = 1;
  s.owner.preferred[1] = 1;
  s.owner.preferred[2] = 0;
  s.owner.preferred[3] = 1;
  list.push_back(s);

  // Day parts agree; the second day part appears only from trial 80.
  s = Scenario{"new_daypart", base(4), trials(160)};
  s.owner.preferred[0] = 1;
  s.owner.preferred[1] = 1;
  s.owner.preferred[2] = 0;
  s.owner.preferred[3] = 0;
  s.bench.lateContextsFrom = 80;
  list.push_back(s);
  return list;
}

struct Candidate {
  char name[64];
  LearnerV3Config config;
};

uint32_t q16(double value) { return static_cast<uint32_t>(value * 65536.0 + 0.5); }

std::vector<Candidate> candidates() {
  std::vector<Candidate> list;
  const double gammas[] = {0.90, 0.925, 0.95};
  const double ks[] = {1, 2, 4};
  const double ms[] = {1, 2, 3};
  const double bonuses[] = {0.15, 0.25, 0.35};
  for (double g : gammas)
    for (double k : ks)
      for (double m : ms)
        for (double b : bonuses) {
          Candidate c;
          c.config.policy = ExplorePolicy::Ucb;
          c.config.gammaQ16 = q16(g);
          c.config.priorStrengthQ16 = q16(k);
          c.config.fineMinQ16 = q16(m);
          c.config.ucbBonusQ16 = q16(b);
          std::snprintf(c.name, sizeof(c.name), "C g=%.3f k=%.0f m=%.0f b=%.2f",
                        g, k, m, b);
          list.push_back(c);
        }
  const double gammasAD[] = {0.90, 0.95};
  for (double g : gammasAD)
    for (int aware = 0; aware <= 1; ++aware)
      for (double eps : {0.15, 0.30}) {
        Candidate c;
        c.config.policy = ExplorePolicy::EpsilonGreedy;
        c.config.gammaQ16 = q16(g);
        c.config.feedbackAware = aware != 0;
        c.config.epsilonMaxQ16 = q16(eps);
        std::snprintf(c.name, sizeof(c.name), "A g=%.3f aware=%d eps=%.2f", g,
                      aware, eps);
        list.push_back(c);
      }
  for (double g : gammasAD)
    for (int aware = 0; aware <= 1; ++aware)
      for (uint32_t gain : {1U, 2U, 4U, 8U})
        for (double conf : {2.0, 5.0, 10.0}) {
          Candidate c;
          c.config.policy = ExplorePolicy::LearningProgress;
          c.config.gammaQ16 = q16(g);
          c.config.feedbackAware = aware != 0;
          c.config.progressGain = gain;
          c.config.confidenceQ16 = q16(conf);
          std::snprintf(c.name, sizeof(c.name),
                        "D g=%.3f aware=%d gain=%u c=%.0f", g, aware, gain,
                        conf);
          list.push_back(c);
        }
  return list;
}

}  // namespace

int main(int argc, char** argv) {
  uint32_t seeds = 200;
  if (argc == 3 && std::strcmp(argv[1], "--seeds") == 0) {
    seeds = static_cast<uint32_t>(std::strtoul(argv[2], 0, 10));
    if (seeds == 0) seeds = 1;
  } else if (argc != 1) {
    std::fprintf(stderr, "usage: learner_sweep [--seeds N]\n");
    return 2;
  }

  const std::vector<Scenario> list = scenarios();
  std::printf("config");
  for (const Scenario& s : list) std::printf(",%s", s.name);
  std::printf(",noisy_flips,late_correct\n");

  for (const Candidate& candidate : candidates()) {
    V3BenchLearner learner(candidate.config);
    std::printf("%s", candidate.name);
    double noisyFlips = 0, lateCorrect = 0, lateSeen = 0;
    for (const Scenario& s : list) {
      const OwnerModel owner(s.owner);
      double regret = 0;
      for (uint32_t seed = 1; seed <= seeds; ++seed) {
        const BenchResult r = LearnerBench::run(learner, owner, s.bench, seed);
        regret += r.regretQ16 / 65536.0;
        if (std::strcmp(s.name, "noisy") == 0 &&
            r.trialsToTarget != LearnerBench::kNever)
          noisyFlips += r.flipsAfterTarget;
        lateCorrect += r.lateCorrect;
        lateSeen += r.lateSeen;
      }
      std::printf(",%.3f", regret / seeds);
    }
    std::printf(",%.2f,%.3f\n", noisyFlips / seeds,
                lateSeen > 0 ? lateCorrect / lateSeen : 0.0);
  }
  return 0;
}
