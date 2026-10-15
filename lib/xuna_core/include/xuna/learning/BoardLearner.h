#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/learning/LearnerV3Codec.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/learning/PreferenceLearnerV3.h>
#include <xuna/life/LifeCommit.h>

namespace xuna {

// Board trial seam (owner shortcut, 2026-10-15): learner v3 decides what XUNA
// shows, while v1 LifeEngine stays the durable record in the existing 96-byte
// snapshot. At boot v3 is rebuilt from v1 with the capped migration (E10-C).
// Known limits until LifeSnapshot v2 (S12a/D2): recency (discounting) is lost
// across reboots, and only quiet/play × idle/happy fit the v1 snapshot.
class BoardLearner {
 public:
  enum class Result : uint8_t { Applied = 0, Rejected, CommitFailed };

  explicit BoardLearner(uint64_t seed)
      : learner_(v1CompatibleLearnerConfig()), random_(seed, 7) {}

  // Tie-break randomness only; the device seeds it from hardware entropy.
  void reseed(uint64_t seed) { random_ = Pcg32(seed, 7); }

  void restore(const LifeEngine& life) {
    LearnerV3Codec::migrateFromV1(life, learner_);
    pending_ = V3Decision();
  }

  // Chooses the action to show in a v1 context (0 quiet, 1 play). The caller
  // still validates it through ActionGate; the decision only becomes
  // rateable through accept().
  uint8_t choose(uint8_t context) {
    candidate_ = learner_.choose(context, 0, nextId_++, 0x03, random_);
    if (nextId_ == 0) nextId_ = 1;
    return candidate_.action;
  }
  void accept() { pending_ = candidate_; }
  void invalidate() { pending_ = V3Decision(); }

  // Plan §9.5: validate, stage v1 with the feedback, commit, then apply v1
  // and v3. A failed commit changes nothing and keeps the decision pending.
  Result feedback(LifeEngine& life, bool approved, uint8_t source,
                  ILifeCommit& commit) {
    if (source < 1 || source > 2 || pending_.id == 0 ||
        !learner_.accepts(pending_))
      return Result::Rejected;
    V3Decision shown = pending_;
    shown.predictedQ16 =
        learner_.view(shown.interaction, shown.dayPart, shown.action).pQ16;
    LifeEngine staged = life;
    staged.feedback(shown.interaction, shown.action, approved, source);
    if (!commit.commit(staged)) return Result::CommitFailed;
    life = staged;
    learner_.feedback(shown, approved);
    pending_ = V3Decision();
    return Result::Applied;
  }

  void forget() {
    learner_.reset(learner_.config());
    pending_ = candidate_ = V3Decision();
  }

  uint8_t preferred(uint8_t context) const {
    return learner_.greedy(context, 0);
  }
  bool hasPending() const { return pending_.id != 0; }
  const V3Decision& pending() const { return pending_; }
  const PreferenceLearnerV3& learner() const { return learner_; }

 private:
  PreferenceLearnerV3 learner_;
  Pcg32 random_;
  V3Decision candidate_;
  V3Decision pending_;
  uint32_t nextId_ = 1;
};

}  // namespace xuna
