#pragma once
#include <xuna/CoreConfig.h>
#include <xuna/visual/VisualIntent.h>
namespace xuna {
enum class PetState : uint8_t { Idle, Happy, Sleep, Curious };
class AnimationManager {
 public:
  void begin(uint32_t now) { setState(PetState::Idle, now); }
  void setIntent(VisualIntent intent, uint8_t stage, uint32_t now) {
    const ExpressionResolution resolved = resolveVisualIntent(intent, stage);
    switch (resolved.resolved) {
      case VisualIntent::Happy:
      case VisualIntent::Celebrate: setState(PetState::Happy, now); break;
      case VisualIntent::Sleep:
      case VisualIntent::Rest:
      case VisualIntent::ErrorSafe: setState(PetState::Sleep, now); break;
      case VisualIntent::Curious:
      case VisualIntent::Greet: setState(PetState::Curious, now); break;
      case VisualIntent::Idle:
      case VisualIntent::Unknown: setState(PetState::Idle, now); break;
    }
  }
  void setState(PetState state, uint32_t now) {
    state_ = state; frame_ = 0; stateAt_ = frameAt_ = now; dirty_ = true;
  }
  // How long a happy / curious reaction stays on screen (personality
  // expression, PE-2); kHappyDurationMs by default.
  void setReactionMs(uint32_t ms) { reactionMs_ = ms; }
  bool update(uint32_t now) {
    if ((state_ == PetState::Happy || state_ == PetState::Curious) && uint32_t(now - stateAt_) >= reactionMs_)
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
  uint32_t reactionMs_ = kHappyDurationMs;
  bool dirty_ = true;
};
}
