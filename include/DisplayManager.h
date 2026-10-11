#pragma once
#include <TFT_eSPI.h>
#include "AnimationManager.h"
namespace xuna {
class DisplayManager {
 public:
  bool begin();
  void render(PetState state, uint8_t frame);
 private:
  TFT_eSPI lcd_;
  TFT_eSprite sprite_{&lcd_};
  bool ready_ = false;
};
}
