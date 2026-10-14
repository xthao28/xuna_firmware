#pragma once

#include <cstdint>

#include <xuna/LifeEngine.h>
#include <xuna/base/Random.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/Needs.h>
#include <xuna/life/RequestValidator.h>
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
    acceptedMask_ = 0;
    for (uint8_t i = 0; i < 4; ++i) lastAcceptedAt_[i] = 0;
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

    needs_.update(input.now);
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
        pending_ = bridge_.choose(event.context, event.id, now);
        pendingValid_ = pending_.candidate.id != 0;
        if (!pendingValid_) return;
        output.selectedAction = pending_.baselineAction;
        output.actionAccepted = requestAction(pending_.baselineAction, 0, now);
        output.baselineAction = pending_.baselineAction;
        output.candidateAction = pending_.candidate.action;
        output.decisionId = pending_.id;
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
    ValidatorContext context;
    context.activity = activity_.state();
    context.energy = needs_.state().energy;
    context.now = now;
    context.acceptedMask = acceptedMask_;
    for (uint8_t i = 0; i < 4; ++i)
      context.lastAcceptedAt[i] = lastAcceptedAt_[i];
    const ValidationResult validation =
        RequestValidator::validate(ActionRequest(action, parameter), context);
    if (!validation.allowed) return false;

    ActivityGuards guards;
    guards.energy = needs_.state().energy;
    guards.restoreOk = true;
    guards.explicitWake = true;
    if (activity_.state() != validation.target &&
        !activity_.request(validation.target, guards, now).ok)
      return false;

    const uint8_t index = static_cast<uint8_t>(validation.action);
    if (index < 4) {
      acceptedMask_ |= static_cast<uint8_t>(1U << index);
      lastAcceptedAt_[index] = now;
    }
    lastVisual_ = validation.visual;
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
  uint8_t acceptedMask_ = 0;
  uint32_t lastAcceptedAt_[4] = {};
  VisualIntent lastVisual_ = VisualIntent::Unknown;
};

}  // namespace xuna
