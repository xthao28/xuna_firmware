#pragma once
#include <cstdint>
#include <xuna/base/Random.h>
#include <xuna/time/ClockModel.h>
namespace xuna {

// What the window shows. Auto follows the trusted day part; without a phone
// sync since boot the day part is Unknown and Auto shows Day (the original art).
enum class Backdrop : uint8_t { Day, Evening, Night, Rain };
enum class SceneMode : uint8_t { Auto, Day, Evening, Night, Rain };
constexpr uint8_t kSceneModeCount = 5;

// Owner hold on GPIO14 cycles Auto → Day → Evening → Night → Rain → Auto.
inline SceneMode nextSceneMode(SceneMode mode) {
  return static_cast<SceneMode>((static_cast<uint8_t>(mode) + 1) % kSceneModeCount);
}
// Stored values outside the range (or never written) fall back to Auto.
inline SceneMode sceneModeFrom(uint8_t value) {
  return value < kSceneModeCount ? static_cast<SceneMode>(value) : SceneMode::Auto;
}
inline Backdrop backdropFor(SceneMode mode, DayPart part) {
  switch (mode) {
    case SceneMode::Day: return Backdrop::Day;
    case SceneMode::Evening: return Backdrop::Evening;
    case SceneMode::Night: return Backdrop::Night;
    case SceneMode::Rain: return Backdrop::Rain;
    case SceneMode::Auto: break;
  }
  if (part == DayPart::Evening) return Backdrop::Evening;
  if (part == DayPart::Night) return Backdrop::Night;
  return Backdrop::Day;
}

// Short overlay above XUNA's head: a heart when a pet reaction taught it, a
// puff when a shoo did, a sparkle when the owner changed the scene. It rises,
// blinks out and ends; a new effect replaces the running one.
enum class EffectKind : uint8_t { None, Heart, Puff, Sparkle };
struct EffectSample {
  EffectKind kind;
  uint8_t rise;
  bool visible;
  bool operator==(const EffectSample& o) const {
    return kind == o.kind && rise == o.rise && visible == o.visible;
  }
  bool operator!=(const EffectSample& o) const { return !(*this == o); }
};
class ReactionEffect {
 public:
  static constexpr uint32_t kDurationMs = 1500;
  static constexpr uint32_t kRiseStepMs = 80;
  static constexpr uint8_t kMaxRise = 14;
  static constexpr uint32_t kBlinkFromMs = 1100;
  static constexpr uint32_t kBlinkMs = 100;
  void start(EffectKind kind, uint32_t now) { kind_ = kind; startAt_ = now; }
  EffectSample sample(uint32_t now) {
    if (kind_ == EffectKind::None) return {EffectKind::None, 0, false};
    const uint32_t elapsed = uint32_t(now - startAt_);
    if (elapsed >= kDurationMs) { kind_ = EffectKind::None; return {EffectKind::None, 0, false}; }
    const uint32_t steps = elapsed / kRiseStepMs;
    const uint8_t rise = steps > kMaxRise ? kMaxRise : uint8_t(steps);
    const bool visible = elapsed < kBlinkFromMs || ((elapsed - kBlinkFromMs) / kBlinkMs) % 2 == 0;
    return {kind_, rise, visible};
  }
 private:
  EffectKind kind_ = EffectKind::None;
  uint32_t startAt_ = 0;
};

// Window life: falling rain drops (Rain) and twinkling stars (Night). Drop
// coordinates are relative to the window glass (width × height).
struct RainDrop { uint8_t x, y; };
class WindowWeather {
 public:
  static constexpr uint8_t kDrops = 14;
  static constexpr uint32_t kRainStepMs = 70;
  static constexpr uint8_t kFall = 4;
  static constexpr uint32_t kTwinkleMs = 600;
  void begin(uint8_t width, uint8_t height, uint32_t now, IRandom& random) {
    width_ = width ? width : 1; height_ = height ? height : 1;
    for (RainDrop& d : drops_) { d.x = uint8_t(random.below(width_)); d.y = uint8_t(random.below(height_)); }
    rainAt_ = twinkleAt_ = now;
    stars_ = 0xff;
  }
  // True when the drops moved (one step per kRainStepMs; late calls catch up
  // by at most one screen height so a stall never loops for long).
  bool stepRain(uint32_t now, IRandom& random) {
    uint32_t steps = uint32_t(now - rainAt_) / kRainStepMs;
    if (!steps) return false;
    rainAt_ += steps * kRainStepMs;
    const uint32_t cap = height_ / kFall + 1;
    if (steps > cap) steps = cap;
    while (steps--) {
      for (RainDrop& d : drops_) {
        const uint16_t y = uint16_t(d.y) + kFall;
        if (y >= height_) {
          d.y = uint8_t(y - height_);
          d.x = uint8_t(random.below(width_));
        } else {
          d.y = uint8_t(y);
          d.x = d.x ? uint8_t(d.x - 1) : uint8_t(width_ - 1);  // slight slant
        }
      }
    }
    return true;
  }
  // True when one star changed; bit i of stars() set = star i bright.
  bool stepStars(uint32_t now, uint8_t starCount, IRandom& random) {
    if (uint32_t(now - twinkleAt_) < kTwinkleMs || !starCount) return false;
    twinkleAt_ = now;
    stars_ ^= uint8_t(1u << random.below(starCount > 8 ? 8 : starCount));
    return true;
  }
  const RainDrop* drops() const { return drops_; }
  uint8_t stars() const { return stars_; }
 private:
  RainDrop drops_[kDrops] = {};
  uint8_t width_ = 1, height_ = 1, stars_ = 0xff;
  uint32_t rainAt_ = 0, twinkleAt_ = 0;
};
}
