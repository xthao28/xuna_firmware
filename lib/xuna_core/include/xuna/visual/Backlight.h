#pragma once
#include <cstdint>
#include <xuna/AnimationManager.h>
namespace xuna {
// Backlight level for the T-Display-S3 LED driver: 16 steps, 16 = full (heat,
// 2026-10-24). Presentation only. Awake sits a little below full; a sleeping
// or resting XUNA dims the screen; cards show at full so text and the QR stay
// easy to read. Levels change one step at a time, so changes fade in.
struct BacklightLevels {
  uint8_t awake = 12; // 75 % LED current, looks ~90 % as bright
  uint8_t asleep = 7; // sleeping / resting
  uint8_t card = 16;  // status / pairing cards
};
class BacklightDimmer {
public:
  static constexpr uint8_t kMax = 16;
  static constexpr uint32_t kStepMs = 60; // awake ↔ asleep fades in ~0.3 s
  explicit BacklightDimmer(BacklightLevels levels = BacklightLevels{})
      : levels_(levels) {}
  uint8_t target(PetState state, bool cardOpen) const {
    const uint8_t level = cardOpen                   ? levels_.card
                          : state == PetState::Sleep ? levels_.asleep
                                                     : levels_.awake;
    return level < 1      ? 1
           : level > kMax ? kMax
                          : level; // 0 would switch the driver off
  }
  // The level to show now. Cards jump straight to their level.
  uint8_t update(uint32_t now, PetState state, bool cardOpen) {
    const uint8_t want = target(state, cardOpen);
    if (cardOpen || level_ == want) {
      level_ = want;
      steppedAt_ = now;
      return level_;
    }
    if (uint32_t(now - steppedAt_) < kStepMs)
      return level_;
    steppedAt_ = now;
    level_ = level_ < want ? uint8_t(level_ + 1) : uint8_t(level_ - 1);
    return level_;
  }
  uint8_t level() const { return level_; }
  bool fading(PetState state, bool cardOpen) const {
    return level_ != target(state, cardOpen);
  }

private:
  BacklightLevels levels_;
  uint8_t level_ = kMax; // the driver powers up at full
  uint32_t steppedAt_ = 0;
};
// Adaptive frame rate & loop pacing (Thermal Phase 1, 2026-10-29): keeps
// 10 FPS (~100 ms/frame) and a 10 ms loop while interacting or playing active
// visuals, then drops to 5 FPS (2x frame period, ~200 ms/frame) and a 25 ms
// loop after 20 s of quiet Idle or Sleep.
class PowerPacer {
public:
  static constexpr uint32_t kQuietAfterMs = 20000;
  static constexpr uint32_t kActiveLoopMs = 10;
  static constexpr uint32_t kQuietLoopMs = 25;
  static constexpr uint16_t kQuietFrameScale = 2;
  void touch(uint32_t now) { activeAt_ = now; }
  bool isQuiet(uint32_t now, PetState state, bool visualBusy) const {
    if (visualBusy)
      return false;
    if (state != PetState::Idle && state != PetState::Sleep)
      return false;
    return uint32_t(now - activeAt_) >= kQuietAfterMs;
  }
  uint16_t framePeriodMs(uint32_t now, PetState state, bool visualBusy,
                         uint16_t basePeriodMs) const {
    if (basePeriodMs == 0)
      return 0;
    return isQuiet(now, state, visualBusy)
               ? uint16_t(basePeriodMs * kQuietFrameScale)
               : basePeriodMs;
  }
  uint32_t loopPeriodMs(uint32_t now, PetState state, bool visualBusy,
                        bool rawButtonDown = false) const {
    return (rawButtonDown || !isQuiet(now, state, visualBusy)) ? kActiveLoopMs
                                                               : kQuietLoopMs;
  }

private:
  uint32_t activeAt_ = 0;
};
} // namespace xuna
