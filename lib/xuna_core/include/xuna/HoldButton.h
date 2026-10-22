#pragma once
#include <cstdint>
namespace xuna {
// Splits one debounced button into a short press (reported on release) and a
// hold (reported once, while still held, after kHoldMs). A hold never also
// produces a short press. All time math wraps safely.
struct HoldEvents { bool shortPress; bool hold; };
class HoldButton {
 public:
  static constexpr uint32_t kHoldMs = 800;
  // A button already held at startup must be released before it counts.
  void begin(bool pressed) { down_ = held_ = pressed; }
  HoldEvents update(bool pressed, uint32_t now) {
    HoldEvents events{false, false};
    if (pressed && !down_) { down_ = true; held_ = false; downAt_ = now; }
    else if (pressed && !held_ && uint32_t(now - downAt_) >= kHoldMs) {
      held_ = true; events.hold = true;
    } else if (!pressed && down_) {
      down_ = false;
      events.shortPress = !held_;
    }
    return events;
  }
 private:
  bool down_ = false, held_ = false;
  uint32_t downAt_ = 0;
};
}
