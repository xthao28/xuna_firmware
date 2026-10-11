#pragma once
#include "DebouncedButton.h"
namespace xuna {
struct InputEvents { bool happy; bool idle; };
class InputManager {
 public:
  void begin(uint32_t now);
  InputEvents update(uint32_t now);
 private:
  DebouncedButton happy_, idle_;
};
}
