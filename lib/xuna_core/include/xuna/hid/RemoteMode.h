#pragma once
#include <cstdint>
#include <xuna/hid/ConsumerReport.h>

namespace xuna {
namespace hid {

enum class RemoteLayout : uint8_t {
  Media = 0,
  Workflows = 1,
};

struct RemoteInput {
  bool happy = false;
  bool idle = false;
  bool happyHold = false;
  bool idleHold = false;
  bool bothHold = false;
};

struct RemoteOutput {
  ConsumerKey key = ConsumerKey::None;
  uint8_t workflowSlot = 0;
  bool enter = false;
  bool exit = false;
  bool advanceScene = false;
  bool stop = false;
  bool consumed = false;
};

class RemoteMode {
 public:
  static constexpr uint32_t kTimeoutMs = 60000;

  bool active() const { return active_; }
  RemoteLayout layout() const { return layout_; }
  void setLayout(RemoteLayout layout) { layout_ = layout; }

  template <typename Events>
  RemoteOutput update(const Events& events, bool hidConnected, uint32_t now, bool inEgg = false) {
    RemoteOutput out;
    if (!active_) {
      if (events.idleHold && hidConnected && !inEgg) {
        active_ = true;
        lastButtonAt_ = now;
        out.enter = true;
        out.consumed = true;
      }
      return out;
    }

    if (!hidConnected || inEgg) {
      active_ = false;
      out.exit = true;
      return out;
    }

    if (events.bothHold) {
      active_ = false;
      out.exit = true;
      out.stop = true;
      out.consumed = true;
      return out;
    }

    if (events.idleHold) {
      active_ = false;
      out.exit = true;
      out.advanceScene = true;
      out.consumed = true;
      return out;
    }

    if (events.happyHold) {
      lastButtonAt_ = now;
      out.key = ConsumerKey::Previous;
      out.consumed = true;
      return out;
    }

    if (events.happy) {
      lastButtonAt_ = now;
      if (layout_ == RemoteLayout::Workflows) {
        out.workflowSlot = 1;
      } else {
        out.key = ConsumerKey::PlayPause;
      }
      out.consumed = true;
      return out;
    }

    if (events.idle) {
      lastButtonAt_ = now;
      if (layout_ == RemoteLayout::Workflows) {
        out.workflowSlot = 2;
      } else {
        out.key = ConsumerKey::Next;
      }
      out.consumed = true;
      return out;
    }

    if (uint32_t(now - lastButtonAt_) >= kTimeoutMs) {
      active_ = false;
      out.exit = true;
    }
    return out;
  }

 private:
  bool active_ = false;
  RemoteLayout layout_ = RemoteLayout::Media;
  uint32_t lastButtonAt_ = 0;
};

}  // namespace hid
}  // namespace xuna

