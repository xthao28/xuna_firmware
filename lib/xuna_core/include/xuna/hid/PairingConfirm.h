#pragma once
#include <cstdint>

namespace xuna {
namespace hid {

enum class PairingReply : uint8_t {
  None = 0,
  Accept = 1,
  Reject = 2
};

struct PairingAction {
  bool show = false;
  uint32_t passkey = 0;
  PairingReply reply = PairingReply::None;
  uint8_t replyToken = 0xFF;
  bool consumed = false;
};

class PairingConfirm {
 public:
  static constexpr uint32_t kTimeoutMs = 30000;
  static constexpr uint8_t kNoToken = 0xFF;

  bool pending() const { return pending_; }
  uint32_t passkey() const { return passkey_; }
  uint8_t token() const { return token_; }

  PairingAction request(uint32_t passkey, uint8_t token, uint32_t now) {
    PairingAction out;
    if (pending_) {
      out.reply = PairingReply::Reject;
      out.replyToken = token_;
    }
    pending_ = true;
    passkey_ = passkey;
    token_ = token;
    requestedAt_ = now;
    out.show = true;
    out.passkey = passkey;
    return out;
  }

  template <typename Events>
  PairingAction update(const Events& events, uint32_t now) {
    PairingAction out;
    if (!pending_) return out;

    const bool anyButton = events.happy || events.idle || events.happyHold ||
                           events.idleHold || events.bothHold;
    if (anyButton) out.consumed = true;

    if (events.happy) {
      out.reply = PairingReply::Accept;
      out.replyToken = token_;
      clear();
      return out;
    }

    if (events.idle) {
      out.reply = PairingReply::Reject;
      out.replyToken = token_;
      clear();
      return out;
    }

    if (uint32_t(now - requestedAt_) >= kTimeoutMs) {
      out.reply = PairingReply::Reject;
      out.replyToken = token_;
      clear();
    }
    return out;
  }

 private:
  bool pending_ = false;
  uint32_t passkey_ = 0;
  uint8_t token_ = kNoToken;
  uint32_t requestedAt_ = 0;

  void clear() {
    pending_ = false;
    passkey_ = 0;
    token_ = kNoToken;
    requestedAt_ = 0;
  }
};

}  // namespace hid
}  // namespace xuna

