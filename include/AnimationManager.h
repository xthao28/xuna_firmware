#pragma once
#include "AppConfig.h"
namespace xuna {
enum class PetState : uint8_t { Idle, Happy };
class AnimationManager {
 public:
  void begin(uint32_t now) { setState(PetState::Idle, now); }
  void setState(PetState state, uint32_t now) {
    state_ = state; frame_ = 0; stateAt_ = frameAt_ = now; dirty_ = true;
  }
  bool update(uint32_t now) {
    if (state_ == PetState::Happy && uint32_t(now - stateAt_) >= kHappyDurationMs)
      setState(PetState::Idle, now);
    const uint32_t interval = state_ == PetState::Idle ? kIdleFrameMs : kHappyFrameMs;
    const uint32_t steps = uint32_t(now - frameAt_) / interval;
    if (steps) {
      frame_ = static_cast<uint8_t>((frame_ + steps % 2) % 2);
      frameAt_ += steps * interval; // skip stale frames; bounded work
      dirty_ = true;
    }
    const bool result = dirty_; dirty_ = false; return result;
  }
  PetState state() const { return state_; }
  uint8_t frame() const { return frame_; }
 private:
  PetState state_ = PetState::Idle;
  uint8_t frame_ = 0;
  uint32_t stateAt_ = 0, frameAt_ = 0;
  bool dirty_ = true;
};
}
