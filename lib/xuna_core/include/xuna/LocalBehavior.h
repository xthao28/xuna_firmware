#pragma once
#include <xuna/LifeEngine.h>
namespace xuna {
class LocalBehavior {
  uint32_t activityAt_=0;
 public:
  void activity(uint32_t now){activityAt_=now;}
  // Quiet-time routine uses learned preference without a phone or wall clock.
  bool choose(uint32_t now,bool idle,const LifeEngine& life,uint8_t& action){
    return choosePreferred(now,idle,life.choose(0),action);
  }
  // Same routine with the preferred quiet action supplied by the caller (the
  // board trial passes learner v3's greedy quiet action). `waitMs`: quiet time
  // before XUNA acts (PE-2b: Expression::spontaneousWaitMs, from drives).
  bool choosePreferred(uint32_t now,bool idle,uint8_t preferred,uint8_t& action,uint32_t waitMs=60000){
    if(!idle||uint32_t(now-activityAt_)<waitMs)return false;
    activityAt_=now;action=preferred;return true;
  }
};
}
