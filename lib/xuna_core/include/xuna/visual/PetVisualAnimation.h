#pragma once
#include <xuna/AnimationManager.h>
#include <xuna/base/Random.h>
namespace xuna {
// Presentation only. Sheet rows: Idle, Happy, Curious, Sleep; 32 frames each.
class PetVisualAnimation {
 public:
  void begin(uint32_t now,IRandom&){startedAt_=now;lastState_=PetState::Idle;}
  // Frame period (personality tempo, PE-2). The current frame continues, so
  // a change never jumps the sprite.
  void setInterval(uint16_t ms,uint32_t now){
    if(ms==0||ms==interval_)return;
    const uint32_t step=uint32_t(now-startedAt_)/interval_;
    startedAt_=now-step*ms;interval_=ms;
  }
  uint16_t interval() const {return interval_;}
  uint8_t frame(uint32_t now,PetState state,IRandom&){
    if(state!=lastState_){startedAt_=now;lastState_=state;}
    uint8_t row=0;
    switch(state){
      case PetState::Happy:row=1;break;
      case PetState::Curious:row=2;break;
      case PetState::Sleep:row=3;break;
      default:break;
    }
    return row*32+(uint32_t(now-startedAt_)/interval_)%32;
  }
 private:
  uint32_t startedAt_=0;
  uint16_t interval_=100;
  PetState lastState_=PetState::Idle;
};
}
