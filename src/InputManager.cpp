#include <Arduino.h>
#include "InputManager.h"
namespace xuna {
void InputManager::begin(uint32_t now) {
  pinMode(kHappyButtonPin, INPUT_PULLUP);
  pinMode(kIdleButtonPin, INPUT_PULLUP);
  happy_.begin(digitalRead(kHappyButtonPin) == LOW, now);
  idle_.begin(digitalRead(kIdleButtonPin) == LOW, now);
}
InputEvents InputManager::update(uint32_t now) {
  return {happy_.update(digitalRead(kHappyButtonPin) == LOW, now),
          idle_.update(digitalRead(kIdleButtonPin) == LOW, now)};
}
}
