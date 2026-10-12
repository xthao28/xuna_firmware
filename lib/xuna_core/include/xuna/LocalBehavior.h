#pragma once
#include <xuna/LifeEngine.h>
namespace xuna {
class LocalBehavior {
  uint32_t activityAt_=0;
 public:
  void activity(uint32_t now){activityAt_=now;}
  // Quiet-time routine uses learned preference without a phone or wall clock.
  bool choose(uint32_t now,bool idle,const LifeEngine& life,uint8_t& action){
    if(!idle||uint32_t(now-activityAt_)<60000)return false;
    activityAt_=now;action=life.choose(0);return true;
  }
};
}
