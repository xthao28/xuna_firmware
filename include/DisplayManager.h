#pragma once
#include <TFT_eSPI.h>
#include <xuna/visual/PetVisualAnimation.h>
namespace xuna {
class DisplayManager {
 public:
  bool begin();
  void update(uint32_t now,PetState state);
  uint32_t draws() const {return draws_;}
  uint32_t maxDrawUs() const {return maxDrawUs_;}
 private:
  TFT_eSPI lcd_;
  uint16_t* region_=nullptr; // allocated once, fixed full sprite canvas
  Pcg32 random_{42,54};PetVisualAnimation visual_;
  uint8_t lastFrame_=255;
  uint32_t draws_=0,maxDrawUs_=0;
};
}
