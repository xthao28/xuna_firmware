#pragma once
#include <xuna/CoreConfig.h>
namespace xuna {
// Active-low conversion belongs to the GPIO adapter. All time math wraps safely.
class DebouncedButton {
 public:
  void begin(bool pressed, uint32_t now) {
    raw_ = stable_ = pressed;
    changedAt_ = now;
  }
  bool update(bool pressed, uint32_t now) {
    if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
    if (raw_ != stable_ && uint32_t(now - changedAt_) >= kDebounceMs) {
      stable_ = raw_;
      return stable_;
    }
    return false;
  }
  bool pressed() const { return stable_; }
 private:
  bool raw_ = false;
  bool stable_ = false;
  uint32_t changedAt_ = 0;
};
}
