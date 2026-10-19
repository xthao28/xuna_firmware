#pragma once

#include <cstdint>
#include <cstring>

#include <xuna/base/Random.h>
#include <xuna/lab/BoardLab.h>
#include <xuna/learning/LearnerV3Codec.h>

namespace xuna {

// S19b Life Lab scenarios on BoardLab (the board's setup()/loop() replica).
// Each returns at the first broken invariant with a short reason; `passed`
// stays false then. Deterministic for a given seed.
struct ScenarioReport {
  bool passed = false;
  const char* failure = "";
  uint64_t steps = 0;
  uint32_t days = 0;
  uint32_t boots = 0;
  uint32_t powerCuts = 0;
  uint32_t suggestions = 0;
  uint32_t feedbackAcked = 0;    // feedback writes answered ok (0)
  uint32_t feedbackCount = 0;    // durable count at the end
  uint32_t duplicatesRejected = 0;
  uint32_t checkpoints = 0;
  uint32_t syncsAccepted = 0;
  uint32_t activeSeconds = 0;
  uint32_t maxAgeLossSeconds = 0;  // largest RAM → durable loss at a reboot
};

class LifeScenarios {
 public:
  static constexpr uint64_t kHourMs = 3600000ULL;
  static constexpr uint64_t kDayMs = 24 * kHourMs;
  static constexpr uint32_t kUtcBase = 1792026000U;  // 2026-10-15 05:00 UTC
  static constexpr int16_t kTz = 420;               // Asia/Ho_Chi_Minh

  // D: 100 days of life with suggestions, greetings and syncs but no rating.
  // The learner must end bit-identical to its state at boot (the migrated
  // pet's learner; an unhatched device cannot persist at all).
  static ScenarioReport noFeedback(const uint8_t* v1, size_t v1Size,
                                   uint32_t days = 100, uint32_t stepMs = 1000) {
    ScenarioReport r;
    r.days = days;
    BoardLab lab;
    lab.store().seedV1(v1, v1Size);
    if (!lab.boot(0)) return fail(r, "boot not ready");
    ++r.boots;
    const PreferenceLearnerV3 start = lab.core().learner();
    const uint32_t startFeedback = lab.snapshot().counters.feedbackCount;
    const uint8_t startEpisodes = lab.snapshot().episodes.count;
    uint8_t frame[16];
    uint32_t id = 0;
    for (uint64_t t = stepMs; t <= days * kDayMs; t += stepMs) {
      const uint32_t now = uint32_t(t);
      if (t % (3 * kHourMs) == 0) {
        ++id;
        const uint8_t result =
            lab.command(frame, BoardLab::suggest(frame, id, uint8_t(id & 1)), now);
        if (result != 0 && result != 5) return fail(r, "suggest result");
        ++r.suggestions;
      }
      if (t % (5 * kHourMs + 17 * 60000ULL) == 0) lab.greeting(now);
      if (t % kDayMs == 8 * kHourMs &&
          lab.command(frame, BoardLab::timeSync(frame, ++id, utc(t), kTz), now) == 0)
        ++r.syncsAccepted;
      lab.loop(now);
      ++r.steps;
      if (lab.snapshot().counters.feedbackCount != startFeedback)
        return fail(r, "feedback appeared");
    }
    if (lab.checkpointFailures()) return fail(r, "checkpoint failed");
    if (!sameLearner(lab.core().learner(), start))
      return fail(r, "learner changed without feedback");
    if (lab.snapshot().episodes.count != startEpisodes) return fail(r, "episodes recorded");
    // ... and the durable copy after a reboot is the same learner.
    if (!lab.boot(0)) return fail(r, "reboot not ready");
    ++r.boots;
    if (!sameLearner(lab.core().learner(), start)) return fail(r, "stored learner changed");
    return finish(r, lab);
  }

  // E: a migrated v1 pet lives through 10 reboots and 5 power cuts while
  // being rated. No feedback is lost or applied twice, the PetId never
  // changes and the age never goes backwards beyond the 5-minute checkpoint.
  static ScenarioReport rebootsAndPowerCuts(const uint8_t* v1, size_t v1Size,
                                            uint32_t stepMs = 1000) {
    ScenarioReport r;
    BoardLab lab;
    lab.store().seedV1(v1, v1Size);
    uint64_t t = 0;
    uint64_t bootAt = 0;
    if (!lab.boot(0) || lab.identity().state() != IdentityState::Ready)
      return fail(r, "first boot");
    ++r.boots;
    uint8_t petId[16];
    std::memcpy(petId, lab.identity().record().petId, 16);
    uint32_t expectedFeedback = lab.snapshot().counters.feedbackCount;
    uint32_t id = 0;
    uint8_t last[16] = {};
    size_t lastSize = 0;
    const uint32_t kEvents = 15;  // 10 reboots + 5 power cuts
    for (uint32_t event = 0; event < kEvents; ++event) {
      // Two to five hours of life with a rated suggestion every 40 minutes.
      const uint64_t until =
          t + (2 + event % 4) * kHourMs + uint64_t(event * 97 + 13) * 1000;
      while (t < until) {
        t += stepMs;
        const uint32_t now = uint32_t(t - bootAt);
        uint8_t frame[16];
        if ((t / stepMs) % (40 * 60000 / stepMs) == 0) {
          const uint32_t decision = ++id;
          if (lab.command(frame, BoardLab::suggest(frame, decision, uint8_t(event & 1)),
                          now) == 0) {
            ++r.suggestions;
            ++id;
            lastSize = BoardLab::feedback(last, id, decision, (event + id) % 3 != 0);
            const uint8_t result = lab.command(last, lastSize, now);
            if (result == 0) {
              ++r.feedbackAcked;
              ++expectedFeedback;
            } else if (result != 7) {
              return fail(r, "feedback result");
            }
          }
        }
        lab.loop(now);
        ++r.steps;
        if (!invariants(lab, r)) return r;
      }
      const uint32_t ramAge = lab.snapshot().counters.activeSeconds;
      const uint32_t now = uint32_t(t - bootAt);
      if (event % 3 == 2) {
        if (lab.powerCutDuringCheckpoint(now)) return fail(r, "torn write reported ok");
        ++r.powerCuts;
      }
      // Reboot: millis() restarts at 0.
      bootAt = t;
      if (!lab.boot(0)) return fail(r, "reboot not ready");
      ++r.boots;
      if (lab.lastBoot().recovery.result != LoadResult::Ok) return fail(r, "recovery not Ok");
      if (lab.identity().state() != IdentityState::Ready ||
          std::memcmp(petId, lab.identity().record().petId, 16) != 0)
        return fail(r, "PetId changed");
      const uint32_t age = lab.snapshot().counters.activeSeconds;
      if (age > ramAge) return fail(r, "age grew across reboot");
      const uint32_t loss = ramAge - age;
      if (loss > r.maxAgeLossSeconds) r.maxAgeLossSeconds = loss;
      if (loss > V1LifeCoreAdapter::kCheckpointMs / 1000 + 1)
        return fail(r, "age lost more than one checkpoint");
      if (lab.snapshot().counters.feedbackCount != expectedFeedback)
        return fail(r, "durable feedback count differs from acks");
      // The app resends its last rating after reconnecting: never applied twice.
      if (lastSize && lab.command(last, lastSize, 1000) != 0) ++r.duplicatesRejected;
      if (lab.snapshot().counters.feedbackCount != expectedFeedback)
        return fail(r, "duplicate feedback applied");
    }
    if (lab.store().bootCounter() != r.boots) return fail(r, "boot counter");
    return finish(r, lab);
  }

  // Stability: 100 days of a simulated owner (ratings, late ratings,
  // greetings, idle presses, daily syncs, a reboot every 10 days, one forget)
  // with invariants checked after every step. Starts from a migrated v1 pet,
  // like the board (an unhatched device does not learn before hatching).
  static ScenarioReport stability(const uint8_t* v1, size_t v1Size,
                                  uint32_t days = 100, uint64_t seed = 2026,
                                  uint32_t stepMs = 1000) {
    ScenarioReport r;
    r.days = days;
    BoardLab lab;
    lab.store().seedV1(v1, v1Size);
    Pcg32 owner(seed, 5);
    if (!lab.boot(0)) return fail(r, "boot not ready");
    ++r.boots;
    uint64_t bootAt = 0;
    uint32_t id = 0;
    uint32_t pending = 0;
    uint64_t rateAt = 0;
    uint64_t nextAction = kHourMs;
    uint32_t lastGeneration = 0;
    bool forgot = false;
    uint32_t expectedFeedback = lab.snapshot().counters.feedbackCount;
    for (uint64_t t = stepMs; t <= days * kDayMs; t += stepMs) {
      const uint32_t now = uint32_t(t - bootAt);
      uint8_t frame[16];
      if (t >= nextAction) {
        nextAction = t + kHourMs + uint64_t(owner.below(3 * 3600)) * 1000;
        const uint32_t kind = owner.below(10);
        if (kind < 6) {
          const uint32_t decision = ++id;
          if (lab.command(frame, BoardLab::suggest(frame, decision, uint8_t(owner.below(2))),
                          now) == 0) {
            ++r.suggestions;
            pending = decision;
            // Mostly within D3's 120 s window, sometimes late.
            rateAt = t + uint64_t(5 + owner.below(kind == 5 ? 400 : 100)) * 1000;
          }
        } else if (kind < 8) {
          lab.greeting(now);
        } else {
          lab.idleButton(now);
        }
      }
      if (pending && t >= rateAt) {
        const uint8_t result = lab.command(
            frame, BoardLab::feedback(frame, ++id, pending, owner.below(10) < 7), now);
        if (result == 0) {
          ++r.feedbackAcked;
          ++expectedFeedback;
        } else if (result != 4 && result != 8) {
          return fail(r, "feedback result");
        }
        pending = 0;
      }
      if (t % kDayMs == 9 * kHourMs &&
          lab.command(frame, BoardLab::timeSync(frame, ++id, utc(t), kTz), now) == 0)
        ++r.syncsAccepted;
      if (!forgot && t == (days / 2) * kDayMs) {
        if (lab.command(frame, BoardLab::forget(frame, ++id), now) != 0)
          return fail(r, "forget");
        forgot = true;
        expectedFeedback = 0;
        lastGeneration = lab.snapshot().generation;
      }
      lab.loop(now);
      ++r.steps;
      if (!invariants(lab, r)) return r;
      if (lab.snapshot().counters.feedbackCount != expectedFeedback)
        return fail(r, "feedback count differs from acks");
      if (int32_t(lab.snapshot().generation - lastGeneration) < 0)
        return fail(r, "generation went backwards");
      lastGeneration = lab.snapshot().generation;
      if (t % (10 * kDayMs) == 0 && t != days * kDayMs) {
        bootAt = t;
        pending = 0;
        if (!lab.boot(0)) return fail(r, "reboot not ready");
        ++r.boots;
      }
    }
    if (lab.checkpointFailures()) return fail(r, "checkpoint failed");
    if (r.syncsAccepted < days - days / 10 - 1) return fail(r, "syncs rejected");
    return finish(r, lab);
  }

 private:
  static uint32_t utc(uint64_t t) { return kUtcBase + uint32_t(t / 1000); }

  static bool invariants(const BoardLab& lab, ScenarioReport& r) {
    const LifeCoreStepOutput& out = lab.adapter().last();
    const char* broken = 0;
    if (out.activity == Activity::ErrorSafe) broken = "ERROR_SAFE";
    else if (uint8_t(out.activity) >= activityCount()) broken = "activity range";
    else if (lab.snapshot().episodes.count > SnapshotEpisodes::kCapacity)
      broken = "episodes overflow";
    else if (lab.checkpointFailures()) broken = "checkpoint failed";
    if (!broken) return true;
    fail(r, broken);
    return false;
  }

  static bool sameLearner(const PreferenceLearnerV3& a, const PreferenceLearnerV3& b) {
    static uint8_t left[SnapshotCodec::kBudget];
    static uint8_t right[SnapshotCodec::kBudget];
    const size_t na = LearnerV3Codec::encode(a, left, sizeof(left));
    const size_t nb = LearnerV3Codec::encode(b, right, sizeof(right));
    if (!na || na != nb || std::memcmp(left, right, na) != 0) return false;
    const size_t sa = LearnerV3Codec::encodeStated(a, left, sizeof(left));
    const size_t sb = LearnerV3Codec::encodeStated(b, right, sizeof(right));
    return sa == sb && std::memcmp(left, right, sa) == 0;
  }

  static ScenarioReport& fail(ScenarioReport& r, const char* why) {
    r.passed = false;
    r.failure = why;
    return r;
  }

  static ScenarioReport& finish(ScenarioReport& r, BoardLab& lab) {
    r.feedbackCount = lab.snapshot().counters.feedbackCount;
    r.checkpoints = lab.checkpoints();
    r.activeSeconds = lab.snapshot().counters.activeSeconds;
    r.passed = true;
    return r;
  }
};

}  // namespace xuna
