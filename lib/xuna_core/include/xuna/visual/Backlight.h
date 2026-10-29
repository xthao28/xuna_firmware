#pragma once
#include <cstdint>
#include <xuna/AnimationManager.h>
namespace xuna {
enum class BrightnessPreset : uint8_t {
  Low = 0,
  Medium = 1,
  High = 2,
};
inline BrightnessPreset brightnessPresetFrom(uint8_t raw) {
  return raw == 0   ? BrightnessPreset::Low
         : raw == 2 ? BrightnessPreset::High
                    : BrightnessPreset::Medium;
}
enum class ScreenPowerState : uint8_t {
  Active = 0,
  Dim = 1,
  Off = 2,
};
// Backlight level for the T-Display-S3 LED driver: 16 steps, 16 = full (heat,
// 2026-10-24). Presentation only. Awake sits a little below full; a sleeping
// or resting XUNA dims the screen; cards show at full so text and the QR stay
// easy to read. Levels change one step at a time, so changes fade in.
struct BacklightLevels {
  uint8_t awake = 12; // 75 % LED current, looks ~90 % as bright
  uint8_t asleep = 7; // sleeping / resting
  uint8_t card = 16;  // status / pairing cards
  uint8_t dim = 4;    // 30–120 s inactivity pre-sleep dim
  static BacklightLevels forPreset(BrightnessPreset preset) {
    BacklightLevels levels;
    switch (preset) {
    case BrightnessPreset::Low:
      levels.awake = 7;
      levels.asleep = 5;
      levels.card = 14;
      levels.dim = 4;
      break;
    case BrightnessPreset::Medium:
      levels.awake = 12;
      levels.asleep = 7;
      levels.card = 16;
      levels.dim = 4;
      break;
    case BrightnessPreset::High:
      levels.awake = 16;
      levels.asleep = 10;
      levels.card = 16;
      levels.dim = 4;
      break;
    }
    return levels;
  }
};
class BacklightDimmer {
public:
  static constexpr uint8_t kMax = 16;
  static constexpr uint32_t kStepMs = 60; // awake ↔ asleep fades in ~0.3 s
  explicit BacklightDimmer(BacklightLevels levels = BacklightLevels{})
      : levels_(levels) {}
  void setLevels(BacklightLevels levels) { levels_ = levels; }
  void setPreset(BrightnessPreset preset) {
    preset_ = preset;
    levels_ = BacklightLevels::forPreset(preset);
  }
  BrightnessPreset preset() const { return preset_; }
  void syncHardware(uint8_t hwLevel) {
    level_ = hwLevel < 1 ? 1 : (hwLevel > kMax ? kMax : hwLevel);
  }
  uint8_t target(PetState state, bool cardOpen, bool dimmed = false) const {
    const uint8_t base = cardOpen                   ? levels_.card
                         : state == PetState::Sleep ? levels_.asleep
                                                    : levels_.awake;
    const uint8_t clampedBase = base < 1 ? 1 : (base > kMax ? kMax : base);
    if (!cardOpen && dimmed) {
      const uint8_t d =
          levels_.dim < 1 ? 1 : (levels_.dim > kMax ? kMax : levels_.dim);
      return d < clampedBase ? d : clampedBase;
    }
    return clampedBase; // 0 would switch the driver off
  }
  // The level to show now. Cards jump straight to their level.
  uint8_t update(uint32_t now, PetState state, bool cardOpen,
                 bool dimmed = false) {
    const uint8_t want = target(state, cardOpen, dimmed);
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
  bool fading(PetState state, bool cardOpen, bool dimmed = false) const {
    return level_ != target(state, cardOpen, dimmed);
  }

private:
  BacklightLevels levels_;
  BrightnessPreset preset_ = BrightnessPreset::Medium;
  uint8_t level_ = kMax; // the driver powers up at full
  uint32_t steppedAt_ = 0;
};
// Adaptive frame rate, loop pacing & 3-stage screen power (Thermal Phase 1 & 2,
// 2026-10-29):
// - 0..20 s: 10 FPS (~100 ms/frame), 10 ms loop, Active brightness.
// - 20..30 s: 5 FPS (2x frame period), 25 ms loop, Active brightness.
// - 30..120 s: 5 FPS, 25 ms loop, Dim brightness (4/16).
// - >= 120 s (when sleepEnabled): ScreenPowerState::Off (0 mA backlight,
//   ST7789_SLPIN, 0 draws, 50 ms loop).
class PowerPacer {
public:
  static constexpr uint32_t kQuietAfterMs = 20000;
  static constexpr uint32_t kDimAfterMs = 30000;
  static constexpr uint32_t kScreenOffAfterMs = 120000;
  static constexpr uint32_t kActiveLoopMs = 10;
  static constexpr uint32_t kQuietLoopMs = 25;
  static constexpr uint32_t kScreenOffLoopMs = 50;
  static constexpr uint16_t kQuietFrameScale = 2;
  void touch(uint32_t now) { activeAt_ = now; }
  void setSleepEnabled(bool enabled) { sleepEnabled_ = enabled; }
  bool sleepEnabled() const { return sleepEnabled_; }
  bool isQuiet(uint32_t now, PetState state, bool visualBusy) const {
    if (visualBusy)
      return false;
    if (state != PetState::Idle && state != PetState::Sleep)
      return false;
    return uint32_t(now - activeAt_) >= kQuietAfterMs;
  }
  ScreenPowerState screenState(uint32_t now, PetState state,
                               bool overlayOpen = false) const {
    (void)state;
    if (overlayOpen)
      return ScreenPowerState::Active;
    const uint32_t elapsed = uint32_t(now - activeAt_);
    if (sleepEnabled_ && elapsed >= kScreenOffAfterMs)
      return ScreenPowerState::Off;
    if (elapsed >= kDimAfterMs)
      return ScreenPowerState::Dim;
    return ScreenPowerState::Active;
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
                        bool rawButtonDown = false,
                        bool overlayOpen = false) const {
    if (rawButtonDown)
      return kActiveLoopMs;
    if (screenState(now, state, overlayOpen) == ScreenPowerState::Off)
      return kScreenOffLoopMs;
    return isQuiet(now, state, visualBusy) ? kQuietLoopMs : kActiveLoopMs;
  }

private:
  uint32_t activeAt_ = 0;
  bool sleepEnabled_ = true;
};
} // namespace xuna
