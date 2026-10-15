#pragma once

#include <cstdint>

#include <xuna/base/Random.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/learning/PreferenceLearnerV3.h>
#include <xuna/persist/SnapshotStore.h>

namespace xuna {

// Product learning seam (D5 + E10, S18 preparation): learner v3 in the
// 3 × 5 × 3 product shape decides, and LifeSnapshot v2 is the durable record.
// It owns the snapshot so a rating, its episode and the feedback counter are
// committed together (plan §9.5): stage a copy, commit it, adopt it only on
// success. Learner indices 0/1/2 = idle/happy/curious (LearnerVocabulary).
// Day part stays 0 (unknown) until trusted time (S10).
class ProductLearner {
 public:
  enum class Result : uint8_t { Applied = 0, Rejected, CommitFailed, Expired };

  // D3 (owner-approved 2026-10-15): a shown reaction can be rated for 120 s.
  static constexpr uint32_t kFeedbackWindowMs = 120000;

  ProductLearner() : state_(productLearnerConfig()) {}

  // Boot: adopt a recovered (or migrated) snapshot. Pending decisions never
  // survive a reboot (plan §8.4).
  void restore(const LifeSnapshotV2& snapshot) {
    state_ = snapshot;
    invalidate();
  }

  // Chooses among the learner actions whose bit is set in `allowedMask`
  // (callers pass the actions the FSM/cooldowns would accept). Returns an
  // empty decision (id 0) when nothing is allowed or the context is invalid.
  V3Decision choose(LearnInteraction interaction, uint8_t dayPart, uint32_t id,
                    uint8_t allowedMask, IRandom& random) {
    return state_.learner.choose(static_cast<uint8_t>(interaction), dayPart,
                                 id, allowedMask, random);
  }

  // Only a shown decision becomes rateable; a new one replaces the old.
  // `now` is the monotonic millisecond clock (wrap-safe differences).
  void show(const V3Decision& decision, uint32_t now) {
    pending_ = decision;
    shownAt_ = now;
    expiredId_ = 0;
  }
  void invalidate() {
    pending_ = V3Decision();
    expiredId_ = 0;
  }

  // Ends the pending decision once the D3 window has passed (plan §8.4
  // `Expired`). Its ID is remembered so a late rating reports Expired rather
  // than unknown. Returns true when this call expired it.
  bool expire(uint32_t now) {
    if (pending_.id == 0 || uint32_t(now - shownAt_) <= kFeedbackWindowMs)
      return false;
    expiredId_ = pending_.id;
    pending_ = V3Decision();
    return true;
  }

  Result feedback(uint32_t decisionId, bool approved, uint8_t source,
                  uint32_t now, ISnapshotCommit* commit) {
    expire(now);
    if (decisionId != 0 && decisionId == expiredId_) return Result::Expired;
    if (source < 1 || source > 2 || pending_.id == 0 ||
        decisionId != pending_.id || !state_.learner.accepts(pending_))
      return Result::Rejected;
    V3Decision shown = pending_;
    shown.predictedQ16 =
        state_.learner.view(shown.interaction, shown.dayPart, shown.action).pQ16;

    LifeSnapshotV2 staged = state_;
    staged.learner.feedback(shown, approved);  // accepted above
    ++staged.counters.feedbackCount;
    SnapshotEpisodes& episodes = staged.episodes;
    LifeEngine::Episode& episode = episodes.items[episodes.next];
    episode.context = shown.interaction;
    episode.action = shown.action;
    episode.approved = approved ? 1 : 0;
    episode.source = source;
    episodes.next = static_cast<uint8_t>((episodes.next + 1U) %
                                         SnapshotEpisodes::kCapacity);
    if (episodes.count < SnapshotEpisodes::kCapacity) ++episodes.count;

    if (commit != 0 && !commit->commit(staged)) return Result::CommitFailed;
    state_ = staged;
    pending_ = V3Decision();
    return Result::Applied;
  }

  // Forget learned preferences and episodes, keeping counters that describe
  // the pet's life (active time, generation) and the clock watermark. Like
  // v1 `LifeStorage::clear`,
  // the empty snapshot is committed to both A/B slots so an old one cannot
  // come back if the newer slot is later corrupted. RAM is reset after the
  // first durable write; a failed second write is reported, never hidden.
  bool forget(ISnapshotCommit* commit) {
    LifeSnapshotV2 staged(state_.learner.config());
    staged.generation = state_.generation;
    staged.counters = state_.counters;
    staged.counters.feedbackCount = 0;
    staged.clock = state_.clock;  // rollback watermark, not learning
    staged.hasNeeds = state_.hasNeeds;
    staged.needs = state_.needs;
    if (commit != 0 && !commit->commit(staged)) return false;
    state_ = staged;
    pending_ = V3Decision();
    if (commit != 0 && !commit->commit(staged)) return false;
    state_ = staged;
    return true;
  }

  // Greedy action for a context; a day part uses its fine cell once it has
  // enough evidence, else the coarse estimate (plan §9.3).
  uint8_t preferred(LearnInteraction interaction, uint8_t dayPart = 0) const {
    return state_.learner.greedy(static_cast<uint8_t>(interaction), dayPart);
  }
  bool hasPending() const { return pending_.id != 0; }
  const V3Decision& pending() const { return pending_; }
  const LifeSnapshotV2& state() const { return state_; }
  LifeSnapshotV2& state() { return state_; }
  const PreferenceLearnerV3& learner() const { return state_.learner; }

 private:
  LifeSnapshotV2 state_;
  V3Decision pending_;
  uint32_t shownAt_ = 0;
  uint32_t expiredId_ = 0;
};

}  // namespace xuna
