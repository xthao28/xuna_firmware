#pragma once

#include <cstdint>

namespace xuna {

// Monotonic millisecond time. Unsigned subtraction keeps elapsed-time checks
// correct across one uint32_t wrap, provided a dwell is less than 2^31 ms.
using MonoMs = uint32_t;

enum class Activity : uint8_t {
  Booting = 0,
  Idle,
  Observing,
  Reacting,
  Playing,
  Resting,
  Sleeping,
  EvolutionTransition,
  ErrorSafe
};

constexpr uint8_t activityCount() { return 9; }

enum class TransitionReason : uint8_t {
  Accepted = 0,
  Illegal,
  AlreadyInState,
  GuardEnergy,
  GuardStage,
  GuardRestore,
  GuardWake,
  GuardReboot,
  Busy,
  Timeout
};

struct ActivityGuards {
  // Q0.16-like bounded energy: 0 is empty and 65535 is full.
  uint16_t energy = 65535;
  uint8_t stage = 0;
  bool busy = false;
  bool restoreOk = false;
  bool explicitWake = false;
  bool evolutionReady = false;
  bool evolutionComplete = false;
  bool reboot = false;
};

struct TransitionResult {
  bool ok;
  TransitionReason reason;
  Activity from;
  Activity to;
  bool timedOut;
  TransitionResult(bool success = false,
                   TransitionReason why = TransitionReason::Illegal,
                   Activity source = Activity::Booting,
                   Activity target = Activity::Booting,
                   bool timeout = false)
      : ok(success), reason(why), from(source), to(target), timedOut(timeout) {}
};

// Small, table-driven activity state machine. It owns no side effects and is
// intended to be driven by the single device loop; renderers and persistence
// observe the resulting state elsewhere.
class ActivityFsm {
 public:
  static constexpr uint16_t kPlayMin = 32768;
  static constexpr uint16_t kRestThreshold = 16384;
  static constexpr uint16_t kRestExit = 24576;
  static constexpr uint16_t kSleepThreshold = 8192;

  // Proposed M04 placeholder dwell values. Zero means the state is released
  // by an explicit guard/event rather than by a timer.
  static constexpr MonoMs kBootingMaxDwellMs = 5000;
  static constexpr MonoMs kObservingMaxDwellMs = 10000;
  static constexpr MonoMs kReactingMaxDwellMs = 2400;
  static constexpr MonoMs kPlayingMaxDwellMs = 30000;
  static constexpr MonoMs kEvolutionMaxDwellMs = 10000;

  Activity state() const { return state_; }
  MonoMs enteredAt() const { return enteredAt_; }

  TransitionResult request(Activity to, const ActivityGuards& guards,
                           MonoMs now) {
    const Activity from = state_;
    TransitionResult result{false, TransitionReason::Illegal, from, to,
                            false};
    if (!legal(from, to)) return result;
    if (from == to) {
      if (from == Activity::Reacting && !guards.busy) {
        enter(to, now);
        result.ok = true;
        result.reason = TransitionReason::Accepted;
      } else {
        result.reason = (guards.busy ? TransitionReason::Busy
                                     : TransitionReason::AlreadyInState);
      }
      return result;
    }
    if (guards.busy && from != Activity::Booting &&
        to != Activity::ErrorSafe) {
      result.reason = TransitionReason::Busy;
      return result;
    }
    switch (to) {
      case Activity::Idle:
        if (from == Activity::Booting && !guards.restoreOk) {
          result.reason = TransitionReason::GuardRestore;
          return result;
        }
        if (from == Activity::Sleeping && !guards.explicitWake) {
          result.reason = TransitionReason::GuardWake;
          return result;
        }
        if (from == Activity::Resting && guards.energy < kRestExit) {
          result.reason = TransitionReason::GuardEnergy;
          return result;
        }
        if (from == Activity::EvolutionTransition &&
            !guards.evolutionComplete) {
          result.reason = TransitionReason::GuardStage;
          return result;
        }
        break;
      case Activity::ErrorSafe:
        if (from == Activity::Booting && guards.restoreOk) {
          result.reason = TransitionReason::GuardRestore;
          return result;
        }
        break;
      case Activity::Booting:
        if (from == Activity::ErrorSafe && !guards.reboot) {
          result.reason = TransitionReason::Busy;
          return result;
        }
        break;
      case Activity::Playing:
        if (guards.energy < kPlayMin) {
          result.reason = TransitionReason::GuardEnergy;
          return result;
        }
        break;
      case Activity::Resting:
        if (guards.energy >= kRestThreshold) {
          result.reason = TransitionReason::GuardEnergy;
          return result;
        }
        break;
      case Activity::EvolutionTransition:
        if (!guards.evolutionReady || guards.stage == 0) {
          result.reason = TransitionReason::GuardStage;
          return result;
        }
        break;
      case Activity::Observing:
      case Activity::Reacting:
      case Activity::Sleeping:
        break;
    }
    enter(to, now);
    result.ok = true;
    result.reason = TransitionReason::Accepted;
    return result;
  }

  // Applies timer exits and energy/wake exits. Returns true when state changed.
  bool tick(MonoMs now, const ActivityGuards& guards = ActivityGuards()) {
    if (state_ == Activity::Idle) {
      if (guards.energy < kSleepThreshold)
        return timeoutTo(Activity::Sleeping, now);
      if (guards.energy < kRestThreshold)
        return timeoutTo(Activity::Resting, now);
      return false;
    }
    if (state_ == Activity::Resting && guards.energy >= kRestExit)
      return timeoutTo(Activity::Idle, now);
    if (state_ == Activity::Sleeping && guards.explicitWake)
      return timeoutTo(Activity::Idle, now);
    if (state_ == Activity::EvolutionTransition && guards.evolutionComplete)
      return timeoutTo(Activity::Idle, now);

    const MonoMs dwell = maxDwell(state_);
    if (dwell == 0 || static_cast<MonoMs>(now - enteredAt_) < dwell)
      return false;
    switch (state_) {
      case Activity::Booting:
        return timeoutTo(Activity::ErrorSafe, now);
      case Activity::Observing:
      case Activity::Reacting:
      case Activity::Playing:
      case Activity::EvolutionTransition:
        return timeoutTo(Activity::Idle, now);
      case Activity::Idle:
      case Activity::Resting:
      case Activity::Sleeping:
      case Activity::ErrorSafe:
        return false;
    }
    return false;
  }

  static MonoMs maxDwell(Activity state) {
    switch (state) {
      case Activity::Booting: return kBootingMaxDwellMs;
      case Activity::Observing: return kObservingMaxDwellMs;
      case Activity::Reacting: return kReactingMaxDwellMs;
      case Activity::Playing: return kPlayingMaxDwellMs;
      case Activity::EvolutionTransition: return kEvolutionMaxDwellMs;
      case Activity::Idle:
      case Activity::Resting:
      case Activity::Sleeping:
      case Activity::ErrorSafe: return 0;
    }
    return 0;
  }

  static bool legal(Activity from, Activity to) {
    static const bool table[activityCount()][activityCount()] = {
        // Booting, Idle, Observing, Reacting, Playing, Resting, Sleeping,
        // EvolutionTransition, ErrorSafe
        {false, true, false, false, false, false, false, false, true},
        {false, false, true, true, true, true, true, true, false},
        {false, true, false, true, false, false, false, false, false},
        {false, true, false, true, false, false, false, false, false},
        {false, true, false, true, false, false, false, false, false},
        {false, true, false, true, false, false, false, false, false},
        {false, true, false, false, false, false, false, false, false},
        {false, true, false, false, false, false, false, false, false},
        {true, false, false, false, false, false, false, false, false}};
    const uint8_t fromIndex = static_cast<uint8_t>(from);
    const uint8_t toIndex = static_cast<uint8_t>(to);
    return fromIndex < activityCount() && toIndex < activityCount() &&
           table[fromIndex][toIndex];
  }

 private:
  void enter(Activity state, MonoMs now) {
    state_ = state;
    enteredAt_ = now;
  }

  bool timeoutTo(Activity target, MonoMs now) {
    const Activity from = state_;
    if (!legal(from, target)) return false;
    enter(target, now);
    return true;
  }

  Activity state_ = Activity::Booting;
  MonoMs enteredAt_ = 0;
};

}  // namespace xuna
