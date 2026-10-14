#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/life/ActionGate.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/Needs.h>
#include <xuna/learning/LearningBridge.h>
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
  bool hasDecision = false;
  uint32_t decisionId = 0;
  uint8_t baselineAction = 0;
  uint8_t candidateAction = 0;
  uint8_t selectedAction = 0;
};

// M04 composition seam. It keeps the v1 LifeEngine authoritative while the
// v2 learner observes the same decisions through LearningBridge. Persistence
// is represented as an output request; storage and BLE remain adapters.
class LifeCore {
 public:
  explicit LifeCore(uint64_t seed = 1, uint64_t stream = 3)
      : random_(seed, stream), bridge_(life_, candidate_, random_) {
    mood_.reset();
  }

  bool begin(MonoMs now, bool restoreOk) {
    needs_.reset(now);
    mood_.reset();
    candidate_.reset();
    pendingValid_ = false;
    gate_.reset();
    lastVisual_ = VisualIntent::Unknown;
    activity_ = ActivityFsm();
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
  const PreferenceLearnerV2& candidate() const { return candidate_; }
  PreferenceLearnerV2& candidate() { return candidate_; }
  const ActivityFsm& activity() const { return activity_; }

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
        const LearningBridgeDecision decision =
            bridge_.choose(event.context, event.id, now);
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
      case LifeCoreEventKind::Feedback:
        if (!pendingValid_ || event.id != pending_.id) return;
        if (bridge_.feedback(pending_, event.approved, event.source, now)) {
          pendingValid_ = false;
          output.learningFeedbackApplied = true;
          output.persistRequested = true;
          signals.positiveFeedback = event.approved;
          signals.negativeFeedback = !event.approved;
        }
        return;
    }
  }

  bool requestAction(uint8_t action, int16_t parameter, MonoMs now) {
    const ActionOutcome outcome = gate_.request(
        action, parameter, activity_, needs_.state().energy, now);
    if (!outcome.accepted) return false;
    lastVisual_ = outcome.visual;
    lastVisualActivity_ = activity_.state();
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
  PreferenceLearnerV2 candidate_;
  Pcg32 random_;
  LearningBridge bridge_;
  ActivityFsm activity_;
  Needs needs_;
  Mood mood_;
  LearningBridgeDecision pending_;
  bool pendingValid_ = false;
  ActionGate gate_;
  VisualIntent lastVisual_ = VisualIntent::Unknown;
  Activity lastVisualActivity_ = Activity::Booting;
};

}  // namespace xuna
