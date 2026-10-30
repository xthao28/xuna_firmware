#pragma once
#include <cstdint>

namespace xuna {
namespace hid {

// Rate limiter for BLE opcode 12 HidConsumer (UDC-2 §5.2):
// <= 4 presses per 1000 ms AND <= 30 presses per 60000 ms across both sources
// (source 1 = app button, source 2 = brain-approved).
class HidRateLimiter {
 public:
  static constexpr uint8_t kMaxPerSecond = 4;
  static constexpr uint8_t kMaxPerMinute = 30;
  static constexpr uint32_t kSecondMs = 1000;
  static constexpr uint32_t kMinuteMs = 60000;

  void reset() {
    count_ = 0;
    head_ = 0;
  }

  bool allow(uint32_t nowMs) {
    uint8_t inSecond = 0;
    uint8_t inMinute = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      const uint8_t idx = uint8_t((head_ + kMaxPerMinute - 1 - i) % kMaxPerMinute);
      const uint32_t elapsed = uint32_t(nowMs - history_[idx]);
      if (elapsed < kMinuteMs) {
        ++inMinute;
        if (elapsed < kSecondMs) {
          ++inSecond;
        }
      }
    }
    if (inSecond >= kMaxPerSecond || inMinute >= kMaxPerMinute) {
      return false;
    }
    history_[head_] = nowMs;
    head_ = uint8_t((head_ + 1) % kMaxPerMinute);
    if (count_ < kMaxPerMinute) ++count_;
    return true;
  }

 private:
  uint32_t history_[kMaxPerMinute] = {};
  uint8_t count_ = 0;
  uint8_t head_ = 0;
};

}  // namespace hid
}  // namespace xuna

