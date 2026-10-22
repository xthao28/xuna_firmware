#pragma once
#include <cstdint>
#include <xuna/HoldButton.h>
namespace xuna {
// The two device buttons as one pad: per button a short press (on release)
// or a hold, plus a combo hold when both stay down for kComboHoldMs. Once both
// are down together, every single-button event is swallowed until both are
// released again, so a combo never also greets, idles or opens a card.
struct PadEvents { HoldEvents a; HoldEvents b; bool comboHold; };
class ButtonPad {
 public:
  static constexpr uint32_t kComboHoldMs = 3000;
  void begin(bool aPressed, bool bPressed) {
    a_.begin(aPressed); b_.begin(bPressed);
    combo_ = fired_ = aPressed && bPressed;  // a boot-time combo never fires
  }
  PadEvents update(bool aPressed, bool bPressed, uint32_t now) {
    PadEvents events{{false, false}, {false, false}, false};
    const HoldEvents a = a_.update(aPressed, now);
    const HoldEvents b = b_.update(bPressed, now);
    const bool both = aPressed && bPressed;
    if (both && !combo_) { combo_ = true; fired_ = false; comboAt_ = now; }
    const bool swallow = combo_;
    if (combo_ && both && !fired_ && uint32_t(now - comboAt_) >= kComboHoldMs) {
      fired_ = true;
      events.comboHold = true;
    }
    if (combo_ && !aPressed && !bPressed) combo_ = false;
    if (!swallow) { events.a = a; events.b = b; }
    return events;
  }
 private:
  HoldButton a_, b_;
  bool combo_ = false, fired_ = false;
  uint32_t comboAt_ = 0;
};
}
