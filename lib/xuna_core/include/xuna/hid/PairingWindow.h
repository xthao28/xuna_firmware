#pragma once
#include <cstdint>

namespace xuna {
namespace hid {

// Pairing window (UDC-2 §5.1): a numeric-comparison prompt is shown only when
// the window is open; outside it, ESP_GAP_BLE_NC_REQ_EVT is rejected at once.
// The window is open when:
//   (a) the pairing QR card is open on screen (existing both-hold 3 s), OR
//   (b) explicitly opened for 60 s (e.g. opcode 13 arg 3 or QR card opened), OR
//   (c) bondCount == 0 (first setup and after "forget all"), OR
//   (d) the board is in Egg.
// It closes after 60 s or after one successful bond. At most 4 bonds are kept;
// a 5th bond is refused until a slot is forgotten.
class PairingWindow {
 public:
  static constexpr uint32_t kWindowMs = 60000;
  static constexpr uint8_t kMaxBonds = 4;

  void open(uint32_t nowMs) {
    manualOpen_ = true;
    openedAt_ = nowMs;
  }

  void close() { manualOpen_ = false; }

  void onBondSuccess() { manualOpen_ = false; }

  bool isOpen(uint32_t nowMs, uint8_t bondCount, bool inEgg, bool qrCardOpen = false) {
    if (manualOpen_ && uint32_t(nowMs - openedAt_) >= kWindowMs) {
      manualOpen_ = false;
    }
    if (bondCount >= kMaxBonds) return false;
    return manualOpen_ || qrCardOpen || bondCount == 0 || inEgg;
  }

  // Returns true if an incoming pairing request should be shown to the owner.
  // An already-bonded peer re-pairing in its existing slot still requires the
  // window to be open, but is not blocked by bondCount == kMaxBonds.
  bool allowPrompt(uint32_t nowMs, uint8_t bondCount, bool inEgg, bool qrCardOpen,
                   bool peerAlreadyHasSlot = false) {
    if (manualOpen_ && uint32_t(nowMs - openedAt_) >= kWindowMs) {
      manualOpen_ = false;
    }
    if (!peerAlreadyHasSlot && bondCount >= kMaxBonds) return false;
    return manualOpen_ || qrCardOpen || bondCount == 0 || inEgg;
  }

 private:
  bool manualOpen_ = false;
  uint32_t openedAt_ = 0;
};

// On-screen 10 s confirmation for opcode 13 arg 2 ("forget all" bonds, UDC-2 §5.2).
// GPIO0 within 10 s confirms -> Confirmed (kOk); GPIO14 or 10 s timeout -> Expired (kExpired).
class ForgetAllConfirm {
 public:
  static constexpr uint32_t kTimeoutMs = 10000;

  enum class Outcome : uint8_t { None = 0, Confirmed = 1, Expired = 2 };

  struct Result {
    Outcome outcome = Outcome::None;
    uint32_t requestId = 0;
    bool consumed = false;
  };

  bool pending() const { return pending_; }
  uint32_t requestId() const { return requestId_; }

  void start(uint32_t requestId, uint32_t nowMs) {
    pending_ = true;
    requestId_ = requestId;
    startedAt_ = nowMs;
  }

  void cancel() {
    pending_ = false;
    requestId_ = 0;
    startedAt_ = 0;
  }

  template <typename Events>
  Result update(const Events& events, uint32_t nowMs) {
    Result out;
    if (!pending_) return out;

    const bool anyButton = events.happy || events.idle || events.happyHold ||
                           events.idleHold || events.bothHold;
    if (anyButton) out.consumed = true;

    if (events.happy) {
      out.outcome = Outcome::Confirmed;
      out.requestId = requestId_;
      cancel();
      return out;
    }
    if (events.idle) {
      out.outcome = Outcome::Expired;
      out.requestId = requestId_;
      cancel();
      return out;
    }
    if (uint32_t(nowMs - startedAt_) >= kTimeoutMs) {
      out.outcome = Outcome::Expired;
      out.requestId = requestId_;
      cancel();
    }
    return out;
  }

 private:
  bool pending_ = false;
  uint32_t requestId_ = 0;
  uint32_t startedAt_ = 0;
};

}  // namespace hid
}  // namespace xuna

