#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/life/ActionGate.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/Needs.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/learning/LearningBridgeV3.h>
#include <xuna/life/LifeCommit.h>
#include <xuna/life/LifeFeel.h>
#include <xuna/visual/VisualIntent.h>

namespace xuna {

enum class LifeCoreEventKind : uint8_t {
  RequestAction = 0,
  Choose,
  Feedback,
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
  bool hasDecision = false;
  uint32_t decisionId = 0;
  uint8_t baselineAction = 0;
  uint8_t candidateAction = 0;
  uint8_t selectedAction = 0;
};

// M04 composition seam. It keeps the v1 LifeEngine authoritative while the
// v3 learner (plan §9.3, tuned Proposed values) observes the same decisions
// through LearningBridgeV3 as a shadow candidate. The candidate resets on
// boot until persistence v2 exists. Persistence is represented as an output
// request; storage and BLE remain adapters.
class LifeCore {
 public:
  // Defaults are the approved life-feel values (same as the production
  // firmware); Life Lab can pass other tuning.
  explicit LifeCore(uint64_t seed = 1, uint64_t stream = 3,
                    const LifeTuning& tuning = lifeFeelTuning(),
                    const ActivityThresholds& thresholds = lifeFeelThresholds())
      : candidate_(shadowConfig()),
        random_(seed, stream),
        bridge_(life_, candidate_, random_),
        activity_(thresholds),
        needs_(tuning) {
    mood_.reset();
  }

  bool begin(MonoMs now, bool restoreOk) {
    needs_.reset(now);
    mood_.reset();
    candidate_.reset(shadowConfig());
    bridge_.resetMetrics();
    pendingValid_ = false;
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
    mood_.update(needs_.state(), signals);
    // An action visual lasts only while its activity does (e.g. Reacting
    // timing out to Idle falls back to the activity visual).
    if (activity_.state() != lastVisualActivity_)
      lastVisual_ = VisualIntent::Unknown;

    output.started = started_;
    output.activity = activity_.state();
    output.needs = needs_.state();
    output.mood = mood_.state();
    output.visual = visualFor(activity_.state());
    if (lastVisual_ != VisualIntent::Unknown) output.visual = lastVisual_;
    output.hasDecision = pendingValid_;
    if (pendingValid_) {
      output.decisionId = pending_.id;
      output.baselineAction = pending_.baselineAction;
      output.candidateAction = pending_.candidate.action;
    }
    return output;
  }

  const LifeEngine& life() const { return life_; }
  LifeEngine& life() { return life_; }
  const PreferenceLearnerV3& candidate() const { return candidate_; }
  PreferenceLearnerV3& candidate() { return candidate_; }
  const LearningBridgeV3Metrics& shadowMetrics() const {
    return bridge_.metrics();
  }

  // Shadow candidate configuration: Life Lab sweep winner (2026-10-15), all
  // values Proposed. Actions 0/1 match the v1 domain until D5; day part stays
  // unknown until trusted time (S10).
  static LearnerV3Config shadowConfig() { return v1CompatibleLearnerConfig(); }
  const ActivityFsm& activity() const { return activity_; }

  // Urgent-commit port (plan §9.5). Not owned. Null keeps the legacy
  // apply-then-persistRequested path for adapter-free tests.
  void setCommitter(ILifeCommit* commit) { commit_ = commit; }

  static constexpr uint8_t kMaxEvents = 8;

 private:
  void process(const LifeCoreEvent& event, MonoMs now, MoodSignals& signals,
               LifeCoreStepOutput& output) {
    switch (event.kind) {
      case LifeCoreEventKind::RequestAction:
        output.actionAccepted = requestAction(event.action, event.parameter,
                                               now);
        if (output.actionAccepted) signals.interaction = true;
        return;
      case LifeCoreEventKind::Choose: {
        const LearningBridgeV3Decision decision =
            bridge_.choose(event.context, 0, event.id, now);
        if (decision.candidate.id == 0) return;
        output.selectedAction = decision.baselineAction;
        output.actionAccepted = requestAction(decision.baselineAction, 0, now);
        // Matches the BLE adapter: only a shown action can receive feedback;
        // a rejected choice keeps the previous accepted decision pending.
        if (!output.actionAccepted) return;
        pending_ = decision;
        pendingValid_ = true;
        return;
      }
      case LifeCoreEventKind::Feedback: {
        if (!pendingValid_ || event.id != pending_.id) return;
        const LearningBridgeV3::Result result = bridge_.feedback(
            pending_, event.approved, event.source, now, commit_);
        if (result == LearningBridgeV3::Result::CommitFailed) {
          // §9.5: nothing changed in RAM; the decision stays pending so the
          // owner can retry; the adapter reports not_ready (BLE result 7).
          output.feedbackNotReady = true;
          return;
        }
        if (result != LearningBridgeV3::Result::Applied) return;
        pendingValid_ = false;
        output.learningFeedbackApplied = true;
        // With a committer the change is already durable; without one the
        // adapter must persist it afterwards (legacy lab path).
        output.feedbackCommitted = commit_ != 0;
        output.persistRequested = commit_ == 0;
        signals.positiveFeedback = event.approved;
        signals.negativeFeedback = !event.approved;
        return;
      }
    }
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
  LifeEngine life_;
  PreferenceLearnerV3 candidate_;
  Pcg32 random_;
  LearningBridgeV3 bridge_;
  ActivityFsm activity_;
  Needs needs_;
  Mood mood_;
  LearningBridgeV3Decision pending_;
  bool pendingValid_ = false;
  ILifeCommit* commit_ = 0;
  ActionGate gate_;
  VisualIntent lastVisual_ = VisualIntent::Unknown;
  Activity lastVisualActivity_ = Activity::Booting;
};

}  // namespace xuna
