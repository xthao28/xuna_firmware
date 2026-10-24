#pragma once
#include <cstdint>
#include <xuna/AnimationManager.h>
#include <xuna/visual/VisualIntent.h>
namespace xuna {
// Track A presentation (2026-10-24, frames from tools/generate_track_a.py): plays a VisualVariant over the current
// pose. Frame ids continue after the 128 base frames: 128 + variant row × 16.
// Loops (look around, bounce) run two cycles, one-shots one. Any later pose
// change (e.g. the owner sends XUNA to idle) cancels it; the pose it started
// on is the one it plays over (a yawn into sleep starts with the Sleep pose).
class VariantPlayer {
 public:
  static constexpr uint8_t kBaseFrames = 128;
  static constexpr uint8_t kFramesPerVariant = 16;
  void start(VisualVariant v, uint32_t now) {
    if (v == VisualVariant::None) return;
    variant_ = v;
    startedAt_ = now;
    armed_ = false;
  }
  // Frame id to draw, or −1 to draw the base animation.
  int frame(uint32_t now, PetState pose, uint16_t periodMs) {
    if (variant_ == VisualVariant::None || periodMs == 0) return -1;
    if (!armed_) {
      pose_ = pose;
      armed_ = true;
    } else if (pose != pose_) {
      variant_ = VisualVariant::None;
      return -1;
    }
    const bool loop = variant_ == VisualVariant::LookAround || variant_ == VisualVariant::BouncePlay;
    const uint32_t step = uint32_t(now - startedAt_) / periodMs;
    if (step >= uint32_t(kFramesPerVariant) * (loop ? 2U : 1U)) {
      variant_ = VisualVariant::None;
      return -1;
    }
    const uint8_t row = static_cast<uint8_t>(static_cast<uint8_t>(variant_) - 1);
    return kBaseFrames + row * kFramesPerVariant + int(step % kFramesPerVariant);
  }
  VisualVariant playing() const { return variant_; }
 private:
  VisualVariant variant_ = VisualVariant::None;
  uint32_t startedAt_ = 0;
  PetState pose_ = PetState::Idle;
  bool armed_ = false;
};
}
