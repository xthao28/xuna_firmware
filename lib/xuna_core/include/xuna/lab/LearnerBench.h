#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/lab/OwnerModel.h>
#include <xuna/learning/PreferenceLearnerV2.h>
#include <xuna/learning/PreferenceLearnerV3.h>

namespace xuna {

// Closed-loop learner benchmark for Life Lab (plan §9.9, E10). Each trial the
// learner shows an action, the simulated owner may rate it, and the learner
// receives that rating. Host/lab only; no heap, deterministic per seed.
class IBenchLearner {
 public:
  virtual ~IBenchLearner() {}
  virtual void reset(uint8_t contexts, uint8_t actions, uint64_t seed) = 0;
  virtual uint8_t choose(uint8_t context, uint32_t trial) = 0;
  // Feedback for the action returned by the latest choose().
  virtual void feedback(bool approved, uint32_t trial) = 0;
  // Predicted P(approve) for (context, action) before feedback, Q16.
  virtual uint16_t predictQ16(uint8_t context, uint8_t action) const = 0;
  virtual uint8_t greedy(uint8_t context) const = 0;
  virtual uint32_t stateHash() const = 0;
  virtual bool supports(uint8_t contexts, uint8_t actions) const = 0;
};

class RandomBenchLearner : public IBenchLearner {
 public:
  RandomBenchLearner() : random_(1, 3) {}
  void reset(uint8_t, uint8_t actions, uint64_t seed) override {
    actions_ = actions == 0 ? 1 : actions;
    random_ = Pcg32(seed, 3);
  }
  uint8_t choose(uint8_t, uint32_t) override {
    return static_cast<uint8_t>(random_.below(actions_));
  }
  void feedback(bool, uint32_t) override {}
  uint16_t predictQ16(uint8_t, uint8_t) const override { return 32768; }
  uint8_t greedy(uint8_t) const override { return 0; }
  uint32_t stateHash() const override { return 0; }
  bool supports(uint8_t, uint8_t) const override { return true; }

 private:
  Pcg32 random_;
  uint8_t actions_ = 1;
};

// "Always idle" baseline from §9.9 B.
class StaticBenchLearner : public IBenchLearner {
 public:
  void reset(uint8_t, uint8_t, uint64_t) override {}
  uint8_t choose(uint8_t, uint32_t) override { return 0; }
  void feedback(bool, uint32_t) override {}
  uint16_t predictQ16(uint8_t, uint8_t) const override { return 32768; }
  uint8_t greedy(uint8_t) const override { return 0; }
  uint32_t stateHash() const override { return 0; }
  bool supports(uint8_t, uint8_t) const override { return true; }
};

// Production v1: greedy Beta(1,1) mean, two contexts, two actions.
class V1BenchLearner : public IBenchLearner {
 public:
  void reset(uint8_t, uint8_t, uint64_t) override {
    life_ = LifeEngine();
    context_ = 0;
    action_ = 0;
  }
  uint8_t choose(uint8_t context, uint32_t) override {
    context_ = context;
    action_ = life_.choose(context);
    return action_;
  }
  void feedback(bool approved, uint32_t) override {
    life_.feedback(context_, action_, approved, 1);
  }
  uint16_t predictQ16(uint8_t context, uint8_t action) const override {
    if (context > 1 || action > 1) return 32768;
    const uint32_t positive = life_.positive[context][action] + 1U;
    const uint32_t total = positive + life_.negative[context][action] + 1U;
    return static_cast<uint16_t>((uint64_t(positive) << 16) / total);
  }
  uint8_t greedy(uint8_t context) const override {
    return life_.choose(context);
  }
  uint32_t stateHash() const override {
    uint8_t bytes[LifeEngine::kSnapshotSize] = {};
    life_.encode(bytes);
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < LifeEngine::kSnapshotSize; ++i)
      hash = (hash ^ bytes[i]) * 16777619U;
    return hash;
  }
  bool supports(uint8_t contexts, uint8_t actions) const override {
    return contexts <= 2 && actions <= 2;
  }

 private:
  LifeEngine life_;
  uint8_t context_ = 0;
  uint8_t action_ = 0;
};

// Shadow candidate v2: undiscounted cells with a UCB-like bonus.
class V2BenchLearner : public IBenchLearner {
 public:
  V2BenchLearner() : random_(1, 3) {}
  void reset(uint8_t contexts, uint8_t actions, uint64_t seed) override {
    LearningConfig config;
    config.contextCount = contexts;
    config.actionCount = actions;
    learner_.reset(config);
    random_ = Pcg32(seed, 3);
    nextId_ = 1;
    decision_ = LearningDecision();
  }
  uint8_t choose(uint8_t context, uint32_t trial) override {
    decision_ = learner_.choose(context, nextId_++, trial, random_);
    return decision_.action;
  }
  void feedback(bool approved, uint32_t trial) override {
    learner_.feedback(decision_, approved, 1, trial);
  }
  uint16_t predictQ16(uint8_t context, uint8_t action) const override {
    return learner_.posterior(context, action).meanQ16;
  }
  uint8_t greedy(uint8_t context) const override {
    uint8_t best = 0;
    uint16_t bestMean = 0;
    for (uint8_t action = 0; action < learner_.config().actionCount;
         ++action) {
      const uint16_t mean = learner_.posterior(context, action).meanQ16;
      if (action == 0 || mean > bestMean) {
        best = action;
        bestMean = mean;
      }
    }
    return best;
  }
  uint32_t stateHash() const override { return learner_.fingerprint(); }
  bool supports(uint8_t contexts, uint8_t actions) const override {
    return contexts <= PreferenceLearnerV2::kMaxContexts &&
           actions <= PreferenceLearnerV2::kMaxActions;
  }

 private:
  PreferenceLearnerV2 learner_;
  Pcg32 random_;
  uint32_t nextId_ = 1;
  LearningDecision decision_;
};

// Learner v3 candidate (plan §9.3/§9.4). With four bench contexts it uses the
// hierarchy: context c = interaction c/2, day part 1 + c%2. Otherwise each
// bench context is an interaction with unknown day part (coarse cells only).
class V3BenchLearner : public IBenchLearner {
 public:
  explicit V3BenchLearner(ExplorePolicy policy = ExplorePolicy::EpsilonGreedy,
                          uint32_t gammaQ16 = 62259)
      : random_(1, 3) {
    base_.policy = policy;
    base_.gammaQ16 = gammaQ16;
  }
  // Full parameter template (k, m, ε, gain, feedbackAware); shape is set per
  // scenario in reset().
  explicit V3BenchLearner(const LearnerV3Config& base)
      : base_(base), random_(1, 3) {}

  void reset(uint8_t contexts, uint8_t actions, uint64_t seed) override {
    LearnerV3Config config = base_;
    hierarchical_ = contexts == 4;
    config.interactions = hierarchical_ ? 2 : contexts;
    config.dayParts = LearnerV3Config::kMaxDayParts;
    config.actions = actions;
    learner_.reset(config);
    random_ = Pcg32(seed, 3);
    nextId_ = 1;
    decision_ = V3Decision();
  }
  uint8_t choose(uint8_t context, uint32_t) override {
    decision_ = learner_.choose(interaction(context), dayPart(context),
                                nextId_++, 0xFF, random_);
    return decision_.action;
  }
  void feedback(bool approved, uint32_t) override {
    learner_.feedback(decision_, approved);
  }
  uint16_t predictQ16(uint8_t context, uint8_t action) const override {
    return learner_.view(interaction(context), dayPart(context), action).pQ16;
  }
  uint8_t greedy(uint8_t context) const override {
    return learner_.greedy(interaction(context), dayPart(context));
  }
  uint32_t stateHash() const override { return learner_.fingerprint(); }
  bool supports(uint8_t contexts, uint8_t actions) const override {
    return (contexts <= LearnerV3Config::kMaxInteractions || contexts == 4) &&
           actions <= LearnerV3Config::kMaxActions;
  }

 private:
  uint8_t interaction(uint8_t context) const {
    return hierarchical_ ? static_cast<uint8_t>(context / 2) : context;
  }
  uint8_t dayPart(uint8_t context) const {
    return hierarchical_ ? static_cast<uint8_t>(1 + context % 2) : 0;
  }

  LearnerV3Config base_;
  PreferenceLearnerV3 learner_;
  Pcg32 random_;
  uint32_t nextId_ = 1;
  V3Decision decision_;
  bool hierarchical_ = false;
};

struct BenchConfig {
  uint32_t trials = 120;
  uint32_t lastWindow = 20;
  // Consecutive trials with a correct greedy action that count as "learned".
  uint32_t stableRun = 6;
  // Before this trial only even contexts occur (e.g. one day part), so a
  // later context tests generalization from the shared prior. 0 = off.
  uint32_t lateContextsFrom = 0;
};

struct BenchResult {
  bool supported = false;
  uint32_t trials = 0;
  uint64_t regretQ16 = 0;          // Σ (P(best) − P(shown))
  uint32_t lastWindowGreedy = 0;   // greedy == best in the last window
  uint32_t lastWindowShown = 0;    // shown == best in the last window
  uint32_t trialsToTarget = 0;     // from changeAt; UINT32_MAX if never
  uint32_t flipsAfterTarget = 0;   // greedy changes after reaching target
  uint32_t rated = 0;
  uint64_t brierQ32 = 0;           // Σ (prediction − outcome)^2, rated trials
  uint64_t baselineBrierQ32 = 0;   // same for a constant 0.5 prediction
  bool stateUnchanged = false;     // learner state equals a fresh learner
  // Greedy correct on the first 10 appearances of late (odd) contexts.
  uint32_t lateCorrect = 0;
  uint32_t lateSeen = 0;
};

class LearnerBench {
 public:
  static constexpr uint32_t kNever = 0xFFFFFFFFU;

  // `greedyCorrect`, when non-null, receives one 0/1 entry per trial.
  static BenchResult run(IBenchLearner& learner, const OwnerModel& owner,
                         const BenchConfig& config, uint64_t seed,
                         uint8_t* greedyCorrect = 0) {
    BenchResult result;
    const OwnerConfig& oc = owner.config();
    if (!learner.supports(oc.contexts, oc.actions)) return result;
    result.supported = true;
    result.trials = config.trials;

    learner.reset(oc.contexts, oc.actions, seed);
    const uint32_t freshHash = learner.stateHash();
    Pcg32 ownerRandom(seed, 11);

    const uint32_t start = changePoint(oc);
    uint32_t run = 0;
    bool reached = false;
    uint8_t lastGreedy[OwnerConfig::kMaxContexts] = {};
    result.trialsToTarget = kNever;

    for (uint32_t trial = 0; trial < config.trials; ++trial) {
      const uint8_t context = contextFor(trial, oc.contexts, config);
      const uint8_t best = owner.bestAction(trial, context);
      const uint8_t greedy = learner.greedy(context);
      const bool correct = greedy == best;
      if (greedyCorrect) greedyCorrect[trial] = correct ? 1 : 0;
      if (config.lateContextsFrom != 0 && trial >= config.lateContextsFrom &&
          (context % 2U) == 1U && result.lateSeen < 10) {
        ++result.lateSeen;
        if (correct) ++result.lateCorrect;
      }

      if (trial >= start && !reached) {
        run = correct ? run + 1 : 0;
        if (run >= config.stableRun) {
          reached = true;
          result.trialsToTarget = trial + 1 - start;
          for (uint8_t c = 0; c < oc.contexts; ++c)
            lastGreedy[c] = learner.greedy(c);
        }
      } else if (reached && greedy != lastGreedy[context]) {
        ++result.flipsAfterTarget;
        lastGreedy[context] = greedy;
      }

      const uint8_t shown = learner.choose(context, trial);
      result.regretQ16 += owner.approveQ16(trial, context, best) -
                          owner.approveQ16(trial, context, shown);
      if (trial + config.lastWindow >= config.trials) {
        if (correct) ++result.lastWindowGreedy;
        if (shown == best) ++result.lastWindowShown;
      }

      if (!owner.rates(ownerRandom)) continue;
      const bool approved =
          owner.approves(trial, context, shown, ownerRandom);
      const uint32_t outcome = approved ? 65536U : 0U;
      const uint32_t prediction = learner.predictQ16(context, shown);
      const uint64_t error = prediction > outcome ? prediction - outcome
                                                  : outcome - prediction;
      result.brierQ32 += error * error;
      result.baselineBrierQ32 += uint64_t(32768) * 32768U;
      ++result.rated;
      learner.feedback(approved, trial);
    }
    result.stateUnchanged = learner.stateHash() == freshHash;
    return result;
  }

 private:
  static uint8_t contextFor(uint32_t trial, uint8_t contexts,
                            const BenchConfig& config) {
    if (trial < config.lateContextsFrom && contexts >= 2)
      return static_cast<uint8_t>((trial % (contexts / 2U)) * 2U);
    return static_cast<uint8_t>(trial % contexts);
  }

  // Trial from which the owner's best action is the final one. For drift the
  // best action flips when the drift passes its midpoint.
  static uint32_t changePoint(const OwnerConfig& config) {
    if (config.kind == OwnerKind::Reversal) return config.changeAt;
    if (config.kind == OwnerKind::Drift)
      return config.changeAt + config.driftLength / 2U + 1U;
    return 0;
  }
};

}  // namespace xuna
