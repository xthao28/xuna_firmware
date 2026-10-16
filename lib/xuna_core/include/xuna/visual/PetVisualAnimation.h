#pragma once
#include <xuna/AnimationManager.h>
#include <xuna/base/Random.h>
namespace xuna {
// Presentation only. Sheet rows: Idle, Happy, Curious, Sleep; 32 frames each.
class PetVisualAnimation {
 public:
  void begin(uint32_t now,IRandom&){startedAt_=now;lastState_=PetState::Idle;}
  uint8_t frame(uint32_t now,PetState state,IRandom&){
    if(state!=lastState_){startedAt_=now;lastState_=state;}
    uint8_t row=0;
    const uint32_t interval=100;
    switch(state){
      case PetState::Happy:row=1;break;
      case PetState::Curious:row=2;break;
      case PetState::Sleep:row=3;break;
      default:break;
    }
    return row*32+(uint32_t(now-startedAt_)/interval)%32;
  }
 private:
  uint32_t startedAt_=0;
  PetState lastState_=PetState::Idle;
};
}
