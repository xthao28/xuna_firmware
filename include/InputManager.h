#pragma once
#include <xuna/ButtonPad.h>
#include <xuna/DebouncedButton.h>
namespace xuna {
// Short presses act on release so holds can be told apart:
// happy/idle: GPIO0/GPIO14 short press; happyHold: GPIO0 held (status card);
// idleHold: GPIO14 held (scene); bothHold: both held 3 s (pairing QR).
struct InputEvents { bool happy; bool idle; bool happyHold; bool idleHold; bool bothHold; };
class InputManager {
 public:
  void begin(uint32_t now);
  InputEvents update(uint32_t now);
 private:
  DebouncedButton happy_, idle_;
  ButtonPad pad_;
};
}
