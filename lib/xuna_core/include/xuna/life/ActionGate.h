#pragma once

#include <cstdint>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/RequestValidator.h>

namespace xuna {

struct ActionOutcome {
  bool accepted = false;
  // Validator verdict. Accepted with !accepted means the FSM refused a
  // transition guard that the validator does not model (e.g. Resting energy).
  ValidationReason reason = ValidationReason::UnknownAction;
  ActionId action = ActionId::Idle;
  VisualIntent visual = VisualIntent::Idle;
};

// Single action path shared by firmware adapters and LifeCore: validation,
// explicit wake from Sleeping, Sleep routed through Idle, FSM transition and
// per-action cooldown bookkeeping. It owns no transport or renderer state.
class ActionGate {
 public:
  void reset() {
    acceptedMask_ = 0;
    for (uint8_t i = 0; i < 4; ++i) lastAcceptedAt_[i] = 0;
  }

  ActionOutcome request(uint8_t id, int16_t parameter, ActivityFsm& activity,
                        uint16_t energy, MonoMs now) {
    ActionOutcome outcome;
    ValidatorContext context;
    context.activity = activity.state();
    context.energy = energy;
    context.now = now;
    context.acceptedMask = acceptedMask_;
    for (uint8_t i = 0; i < 4; ++i)
      context.lastAcceptedAt[i] = lastAcceptedAt_[i];
    const ActionRequest request(id, parameter);
    const uint8_t sleepId = static_cast<uint8_t>(ActionId::Sleep);
    ValidationResult validation = RequestValidator::validate(request, context);
    bool wake = false;
    bool routeThroughIdle = false;
    if (!validation.allowed && activity.state() == Activity::Sleeping &&
        id != sleepId) {
      context.activity = Activity::Idle;
      validation = RequestValidator::validate(request, context);
      wake = validation.allowed;
    }
    if (!validation.allowed && id == sleepId &&
        activity.state() != Activity::Idle &&
        activity.state() != Activity::Sleeping) {
      context.activity = Activity::Idle;
      validation = RequestValidator::validate(request, context);
      routeThroughIdle = validation.allowed;
    }
    outcome.reason = validation.reason;
    outcome.action = validation.action;
    outcome.visual = validation.visual;
    if (!validation.allowed) return outcome;

    ActivityGuards guards;
    guards.energy = energy;
    guards.restoreOk = true;
    guards.explicitWake = true;
    if ((wake || routeThroughIdle) && activity.state() != Activity::Idle &&
        !activity.request(Activity::Idle, guards, now).ok)
      return outcome;
    // Same-state requests are no-ops, except that a new reaction restarts the
    // Reacting timer (plan §8.1).
    const bool transition = activity.state() != validation.target ||
                            validation.target == Activity::Reacting;
    if (transition && !activity.request(validation.target, guards, now).ok)
      return outcome;

    const uint8_t index = static_cast<uint8_t>(validation.action);
    if (index < 4) {
      acceptedMask_ |= static_cast<uint8_t>(1U << index);
      lastAcceptedAt_[index] = now;
    }
    outcome.accepted = true;
    return outcome;
  }

 private:
  uint8_t acceptedMask_ = 0;
  uint32_t lastAcceptedAt_[4] = {};
};

}  // namespace xuna
