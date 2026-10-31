#pragma once
#include <cstdint>
#include <xuna/hid/ConsumerReport.h>

namespace xuna {
namespace hid {

enum class RemoteLayout : uint8_t {
  Media = 0,
  Workflows = 1,
};

enum class HidHostsArg : uint8_t {
  ForgetSlot = 1,
  ForgetAll = 2,
  OpenPairingWindow = 3,
  SetRemoteLayout = 4,
  PromptApproval = 5,
  CancelApproval = 6,
};

enum class RemoteEventCode : uint8_t {
  None = 0,
  Stop = 1,
  WorkflowSlot1 = 2,
  WorkflowSlot2 = 3,
  ApprovalApprove = 4,
  ApprovalReject = 5,
};

// On-screen R2 action approval prompt (UDC-12 S12.1):
// Triggered by BLE opcode 13 arg=5 (PromptApproval, source=timeoutSec or 0 -> 30s)
// and cancelled by arg=6 (CancelApproval).
// GPIO0 short press -> ApprovalApprove (4); GPIO14 short press -> ApprovalReject (5).
// Timeout -> silently expires (mobile phone handles its own approval TTL).
class ActionApprovalConfirm {
 public:
  static constexpr uint32_t kDefaultTimeoutMs = 30000;
  static constexpr uint8_t kMaxTimeoutSec = 120;

  struct Result {
    RemoteEventCode decision = RemoteEventCode::None;
    bool expired = false;
    bool consumed = false;
  };

  bool pending() const { return pending_; }
  uint32_t timeoutMs() const { return timeoutMs_; }

  void start(uint8_t timeoutSec, uint32_t nowMs) {
    const uint8_t sec = timeoutSec > kMaxTimeoutSec ? kMaxTimeoutSec : timeoutSec;
    timeoutMs_ = sec == 0 ? kDefaultTimeoutMs : uint32_t(sec) * 1000u;
    startedAt_ = nowMs;
    pending_ = true;
  }

  void cancel() {
    pending_ = false;
    startedAt_ = 0;
    timeoutMs_ = kDefaultTimeoutMs;
  }

  template <typename Events>
  Result update(const Events& events, uint32_t nowMs) {
    Result out;
    if (!pending_) return out;

    const bool anyButton = events.happy || events.idle || events.happyHold ||
                           events.idleHold || events.bothHold;
    if (anyButton) out.consumed = true;

    if (events.happy) {
      out.decision = RemoteEventCode::ApprovalApprove;
      cancel();
      return out;
    }
    if (events.idle) {
      out.decision = RemoteEventCode::ApprovalReject;
      cancel();
      return out;
    }
    if (uint32_t(nowMs - startedAt_) >= timeoutMs_) {
      out.expired = true;
      cancel();
    }
    return out;
  }

 private:
  bool pending_ = false;
  uint32_t startedAt_ = 0;
  uint32_t timeoutMs_ = kDefaultTimeoutMs;
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

