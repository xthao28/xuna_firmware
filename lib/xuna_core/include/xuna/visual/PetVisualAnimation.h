#pragma once
#include <xuna/AnimationManager.h>
#include <xuna/base/Random.h>
namespace xuna {
// Presentation only; no pet learning/state/storage mutation. All times wrap as uint32.
class PetVisualAnimation {
 public:
  void begin(uint32_t now,IRandom& random){blinkAt_=wiggleAt_=idleAt_=now;blinkWait_=3000+random.below(2001);wiggleWait_=4000+random.below(2501);blinkStep_=wiggleStep_=0;lastState_=PetState::Idle;}
  uint8_t frame(uint32_t now,PetState state,IRandom& random){
    if(state!=lastState_){blinkStep_=0;blinkAt_=now;lastState_=state;if(state==PetState::Happy){wiggleStep_=1;wiggleAt_=now;}}
    if(!blinkStep_&&uint32_t(now-blinkAt_)>=blinkWait_){blinkStep_=1;blinkAt_=now;}
    if(blinkStep_&&uint32_t(now-blinkAt_)>=100){blinkAt_=now;if(++blinkStep_>3){blinkStep_=0;blinkWait_=3000+random.below(2001);}}
    if(!wiggleStep_&&uint32_t(now-wiggleAt_)>=wiggleWait_){wiggleStep_=1;wiggleAt_=now;}
    if(wiggleStep_&&uint32_t(now-wiggleAt_)>=160){wiggleAt_=now;if(++wiggleStep_>4){wiggleStep_=0;wiggleWait_=4000+random.below(2501);}}
    // Idle deformation changes torso/horns together, feet stay grounded.
    const uint8_t bob=(uint32_t(now-idleAt_)/900)%4==2?1:0;
    uint8_t eyes=blinkStep_; // normal -> half -> closed -> open -> normal
    if(state==PetState::Sleep)eyes=2;
    else if(state==PetState::Happy)eyes=4;
    else if(state==PetState::Curious){if(!blinkStep_)eyes=3;}
    const uint8_t feet=state==PetState::Sleep?0:(wiggleStep_&1);
    return bob*10+eyes*2+feet;
  }
 private:
  uint32_t blinkAt_=0,wiggleAt_=0,idleAt_=0,blinkWait_=3000,wiggleWait_=6500;
  uint8_t blinkStep_=0,wiggleStep_=0;
  PetState lastState_=PetState::Idle;
};
}
