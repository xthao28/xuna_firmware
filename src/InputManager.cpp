#include "BoardConfig.h"
#include <Arduino.h>
#include "InputManager.h"
namespace xuna {
void InputManager::begin(uint32_t now) {
  pinMode(kHappyButtonPin, INPUT_PULLUP);
  pinMode(kIdleButtonPin, INPUT_PULLUP);
  happy_.begin(digitalRead(kHappyButtonPin) == LOW, now);
  idle_.begin(digitalRead(kIdleButtonPin) == LOW, now);
  pad_.begin(happy_.pressed(), idle_.pressed());
}
InputEvents InputManager::update(uint32_t now) {
  const bool rawHappy = digitalRead(kHappyButtonPin) == LOW;
  const bool rawIdle = digitalRead(kIdleButtonPin) == LOW;
  rawPressed_ = rawHappy || rawIdle;
  happy_.update(rawHappy, now);
  idle_.update(rawIdle, now);
  const PadEvents pad = pad_.update(happy_.pressed(), idle_.pressed(), now);
  return {pad.a.shortPress, pad.b.shortPress, pad.a.hold, pad.b.hold, pad.comboHold};
}
}
