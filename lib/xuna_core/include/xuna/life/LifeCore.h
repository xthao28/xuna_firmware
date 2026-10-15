#pragma once

#include <cstdint>

#include <xuna/base/Random.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/learning/ProductLearner.h>
#include <xuna/life/ActionGate.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/LifeFeel.h>
#include <xuna/life/Needs.h>
#include <xuna/persist/SnapshotStore.h>
#include <xuna/time/ClockModel.h>
#include <xuna/visual/VisualIntent.h>

namespace xuna {

enum class LifeCoreEventKind : uint8_t {
  RequestAction = 0,  // direct request (app command, GPIO14 idle button)
  Choose,             // app asks XUNA to choose: context 0 quiet, 1 play
  Feedback,           // explicit rating of the pending decision
  Greeting,           // GPIO0 press: XUNA chooses how to answer (D5)
};

// Core-facing event vocabulary. Transport adapters translate BLE/buttons/text
// into this bounded shape; no Arduino or wire types cross the boundary.
struct LifeCoreEvent {
  LifeCoreEventKind kind = LifeCoreEventKind::RequestAction;
  uint32_t id = 0;
  uint8_t context = 0;
  uint8_t action = 0;
  bool approved = false;
  uint8_t source = 0;
  int16_t parameter = 0;

  LifeCoreEvent(LifeCoreEventKind eventKind = LifeCoreEventKind::RequestAction,
                uint32_t eventId = 0, uint8_t eventContext = 0,
                uint8_t eventAction = 0, bool eventApproved = false,
                uint8_t eventSource = 0, int16_t eventParameter = 0)
      : kind(eventKind),
        id(eventId),
        context(eventContext),
        action(eventAction),
        approved(eventApproved),
        source(eventSource),
        parameter(eventParameter) {}
};

struct LifeCoreStepInput {
  MonoMs now = 0;
  const LifeCoreEvent* events = 0;
  uint8_t eventCount = 0;

  LifeCoreStepInput(MonoMs time = 0, const LifeCoreEvent* inputEvents = 0,
                    uint8_t count = 0)
      : now(time), events(inputEvents), eventCount(count) {}
};

struct LifeCoreStepOutput {
  Activity activity = Activity::Booting;
  NeedsState needs;
  MoodState mood;
  VisualIntent visual = VisualIntent::Unknown;
  bool started = false;
  bool actionAccepted = false;
  bool learningFeedbackApplied = false;
  bool persistRequested = false;
  bool feedbackCommitted = false;  // urgent commit done before applying
  bool feedbackNotReady = false;   // commit failed; RAM unchanged (§9.5)
  bool feedbackExpired = false;    // rating arrived after the D3 window
  bool decisionExpired = false;    // the pending decision timed out this step
  bool decisionShown = false;      // a Choose/Greeting showed a new decision
  uint8_t selectedAction = 0;      // ActionId shown by that decision
  bool selectedExplore = false;    // UCB picked it over the greedy action
  bool hasDecision = false;        // a decision is pending a rating
  uint32_t decisionId = 0;
  uint8_t decisionContext = 0;     // LearnInteraction of the pending decision
  uint8_t decisionDayPart = 0;     // 0 = unknown; 1–4 morning…night
};

// Learner day-part index (plan §9.2): 0 unknown, 1 morning, 2 afternoon,
// 3 evening, 4 night, the same order as ClockModel's DayPart.
inline uint8_t learnDayPart(DayPart part) {
  switch (part) {
    case DayPart::Morning: return 1;
    case DayPart::Afternoon: return 2;
    case DayPart::Evening: return 3;
    case DayPart::Night: return 4;
    case DayPart::Unknown: break;
  }
  return 0;
}

// M04 composition seam (S18 preparation). Learner v3 in the D5 product shape
// (quiet/play/button_greeting × idle/happy/curious, E10 parameters) decides
// through ProductLearner, and LifeSnapshot v2 is the durable record. The
// adapter restores the recovered snapshot before begin() and provides the
// urgent-commit port; storage and BLE remain adapters. Production firmware
// still runs v1 + BoardLearner until S13/S18 wiring.
class LifeCore {
 public:
  // App command IDs stay below this value; device-originated decisions
  // (GPIO0 greeting) use IDs from here up so the two never collide in the
  // learner's duplicate window. Protocol note for S18.
  static constexpr uint32_t kLocalDecisionIdBase = 0x80000000U;
  static constexpr uint8_t kMaxEvents = 8;

  // Defaults are the approved life-feel values (same as the production
  // firmware); Life Lab can pass other tuning.
  explicit LifeCore(uint64_t seed = 1, uint64_t stream = 3,
                    const LifeTuning& tuning = lifeFeelTuning(),
                    const ActivityThresholds& thresholds = lifeFeelThresholds())
      : random_(seed, stream), activity_(thresholds), needs_(tuning) {
    mood_.reset();
  }

  // Adopt the boot-recovered snapshot (Ok/Migrated) before begin().
  void restore(const LifeSnapshotV2& snapshot) { learner_.restore(snapshot); }

  bool begin(MonoMs now, bool restoreOk) {
    // Time is unknown after every boot; only the persisted watermark is kept
    // for rollback detection (plan §8.3).
    lastNow_ = now;
    mono64_ = now;
    clock_.onBoot(mono64_, learner_.state().clock.lastTrustedUtcMinute);
    needs_.reset(now);
    mood_.reset();
    learner_.invalidate();
    gate_.reset();
    lastVisual_ = VisualIntent::Unknown;
    activity_ = ActivityFsm(activity_.thresholds());
    ActivityGuards guards;
    guards.restoreOk = restoreOk;
    guards.energy = needs_.state().energy;
    const Activity target = restoreOk ? Activity::Idle : Activity::ErrorSafe;
    started_ = activity_.request(target, guards, now).ok;
    return started_;
  }

  LifeCoreStepOutput step(const LifeCoreStepInput& input) {
    LifeCoreStepOutput output;
    output.started = started_;
    if (!started_) {
      output.activity = activity_.state();
      output.needs = needs_.state();
      output.mood = mood_.state();
      output.visual = VisualIntent::ErrorSafe;
      return output;
    }

    extendMono(input.now);
    needs_.update(input.now, ActivityFsm::recoversEnergy(activity_.state()));
    MoodSignals signals;
    ActivityGuards guards;
    guards.energy = needs_.state().energy;
    guards.restoreOk = true;
    activity_.tick(input.now, guards);

    const uint8_t count = input.eventCount > kMaxEvents ? kMaxEvents
                                                         : input.eventCount;
    for (uint8_t i = 0; i < count; ++i) {
      if (!input.events) break;
      process(input.events[i], input.now, signals, output);
    }
    // D3: an unrated decision ends 120 s after it was shown.
    if (learner_.expire(input.now)) output.decisionExpired = true;
    mood_.update(needs_.state(), signals);
    // An action visual lasts only while its activity does (e.g. Reacting
    // timing out to Idle falls back to the activity visual).
    if (activity_.state() != lastVisualActivity_)
      lastVisual_ = VisualIntent::Unknown;

    output.activity = activity_.state();
    output.needs = needs_.state();
    output.mood = mood_.state();
    output.visual = visualFor(activity_.state());
    if (lastVisual_ != VisualIntent::Unknown) output.visual = lastVisual_;
    output.hasDecision = learner_.hasPending();
    if (output.hasDecision) {
      output.decisionId = learner_.pending().id;
      output.decisionContext = learner_.pending().interaction;
      output.decisionDayPart = learner_.pending().dayPart;
    }
    return output;
  }

  // TimeSync (future BLE opcode 5, S10). An accepted sync updates the
  // watermark and offset in the RAM snapshot; they reach storage with the
  // next commit (lazy class, plan §7.6). Ignored in ERROR_SAFE.
  SyncResult sync(int64_t utcMs, int16_t tzOffsetMin, TimeSource source,
                  MonoMs now) {
    if (!started_ || activity_.state() == Activity::ErrorSafe)
      return {SyncStatus::RejectedInvalid, false};
    extendMono(now);
    const SyncResult result =
        clock_.onSync(utcMs, tzOffsetMin, source, mono64_);
    if (result.status == SyncStatus::Accepted) {
      SnapshotClock& saved = learner_.state().clock;
      saved.lastTrustedUtcMinute = clock_.lastTrustedUtcMinute();
      saved.tzOffsetMin = tzOffsetMin;
    }
    return result;
  }
  const ClockModel& clock() const { return clock_; }
  // 64-bit monotonic time for ClockModel queries (millis() wraps at ~49.7
  // days; the core extends it on every step and sync).
  uint64_t mono64() const { return mono64_; }

  // Forget (BLE opcode 4): commit-first reset of learning and episodes.
  // ERROR_SAFE is read-only: nothing is written.
  bool forget() {
    if (!started_ || activity_.state() == Activity::ErrorSafe) return false;
    return learner_.forget(commit_);
  }

  const LifeSnapshotV2& snapshot() const { return learner_.state(); }
  LifeSnapshotV2& snapshot() { return learner_.state(); }
  const PreferenceLearnerV3& learner() const { return learner_.learner(); }
  uint8_t preferred(LearnInteraction interaction, uint8_t dayPart = 0) const {
    return learner_.preferred(interaction, dayPart);
  }
  const ActivityFsm& activity() const { return activity_; }

  // Urgent-commit port (plan §9.5). Not owned. Null keeps the legacy
  // apply-then-persistRequested path for adapter-free tests.
  void setCommitter(ISnapshotCommit* commit) { commit_ = commit; }

 private:
  void process(const LifeCoreEvent& event, MonoMs now, MoodSignals& signals,
               LifeCoreStepOutput& output) {
    switch (event.kind) {
      case LifeCoreEventKind::RequestAction:
        output.actionAccepted = requestAction(event.action, event.parameter,
                                               now);
        if (!output.actionAccepted) return;
        signals.interaction = true;
        // MVP rule kept (plan §9.5): direct input invalidates the pending
        // reaction, as the BLE adapter does today.
        learner_.invalidate();
        return;
      case LifeCoreEventKind::Choose:
        if (event.context > 1 || event.id == 0 ||
            event.id >= kLocalDecisionIdBase)
          return;
        decide(static_cast<LearnInteraction>(event.context), event.id, now,
               signals, output);
        return;
      case LifeCoreEventKind::Greeting:
        decide(LearnInteraction::ButtonGreeting, nextLocalId(), now, signals,
               output);
        return;
      case LifeCoreEventKind::Feedback: {
        const ProductLearner::Result result = learner_.feedback(
            event.id, event.approved, event.source, now, commit_);
        if (result == ProductLearner::Result::Expired) {
          output.feedbackExpired = true;  // plan §9.5 rejection `expired`
          return;
        }
        if (result == ProductLearner::Result::CommitFailed) {
          // §9.5: nothing changed in RAM; the decision stays pending so the
          // owner can retry; the adapter reports not_ready (BLE result 7).
          output.feedbackNotReady = true;
          return;
        }
        if (result != ProductLearner::Result::Applied) return;
        output.learningFeedbackApplied = true;
        // With a committer the change is already durable; without one the
        // adapter must persist it afterwards (adapter-free tests).
        output.feedbackCommitted = commit_ != 0;
        output.persistRequested = commit_ == 0;
        signals.positiveFeedback = event.approved;
        signals.negativeFeedback = !event.approved;
        return;
      }
    }
  }

  // The learner only considers actions the FSM and cooldowns would accept
  // right now (plan §9.4 allowed mask). A choice that cannot be shown keeps
  // the previous pending decision, matching the BLE adapter.
  void decide(LearnInteraction interaction, uint32_t id, MonoMs now,
              MoodSignals& signals, LifeCoreStepOutput& output) {
    const uint8_t mask = allowedMask(now);
    if (mask == 0) return;
    // Fine day-part cells are used only with trusted time (Synced or
    // Approximate); otherwise the coarse cell (0) decides and learns.
    const uint8_t dayPart = learnDayPart(clock_.dayPart(mono64_));
    const V3Decision decision =
        learner_.choose(interaction, dayPart, id, mask, random_);
    ActionId action = ActionId::Idle;
    if (decision.id == 0 || !learnActionId(decision.action, action)) return;
    if (!requestAction(static_cast<uint8_t>(action), 0, now)) return;
    learner_.show(decision, now);
    signals.interaction = true;
    output.actionAccepted = true;
    output.decisionShown = true;
    output.selectedAction = static_cast<uint8_t>(action);
    output.selectedExplore = decision.explore;
  }

  uint8_t allowedMask(MonoMs now) const {
    uint8_t mask = 0;
    for (uint8_t index = 0; index < kLearnActionCount; ++index) {
      ActionId action = ActionId::Idle;
      if (!learnActionId(index, action)) continue;
      ActionGate probeGate = gate_;
      ActivityFsm probeFsm = activity_;
      if (probeGate.request(static_cast<uint8_t>(action), 0, probeFsm,
                            needs_.state().energy, now).accepted)
        mask = static_cast<uint8_t>(mask | (1U << index));
    }
    return mask;
  }

  // Wrap-safe 32 → 64-bit extension; callers pass non-decreasing millis().
  void extendMono(MonoMs now) {
    mono64_ += static_cast<MonoMs>(now - lastNow_);
    lastNow_ = now;
  }

  uint32_t nextLocalId() {
    const uint32_t id = nextLocalId_;
    nextLocalId_ = nextLocalId_ == 0xFFFFFFFFU ? kLocalDecisionIdBase
                                                : nextLocalId_ + 1U;
    return id;
  }

  bool requestAction(uint8_t action, int16_t parameter, MonoMs now) {
    const ActionOutcome outcome = gate_.request(
        action, parameter, activity_, needs_.state().energy, now);
    if (!outcome.accepted) return false;
    lastVisual_ = outcome.visual;
    lastVisualActivity_ = activity_.state();
    applyAcceptedAction(needs_, action);
    return true;
  }

  static VisualIntent visualFor(Activity activity) {
    switch (activity) {
      case Activity::Sleeping: return VisualIntent::Sleep;
      case Activity::Resting: return VisualIntent::Rest;
      case Activity::ErrorSafe: return VisualIntent::ErrorSafe;
      case Activity::Reacting:
      case Activity::Playing: return VisualIntent::Happy;
      case Activity::Booting:
      case Activity::Idle:
      case Activity::Observing:
      case Activity::EvolutionTransition: return VisualIntent::Idle;
    }
    return VisualIntent::ErrorSafe;
  }

  bool started_ = false;
  ProductLearner learner_;
  Pcg32 random_;
  ActivityFsm activity_;
  Needs needs_;
  Mood mood_;
  ClockModel clock_;
  MonoMs lastNow_ = 0;
  uint64_t mono64_ = 0;
  ISnapshotCommit* commit_ = 0;
  ActionGate gate_;
  VisualIntent lastVisual_ = VisualIntent::Unknown;
  Activity lastVisualActivity_ = Activity::Booting;
  uint32_t nextLocalId_ = kLocalDecisionIdBase + 1U;
};

}  // namespace xuna
