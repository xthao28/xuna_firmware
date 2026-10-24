#pragma once
#include <cstdint>
#include <xuna/AnimationManager.h>
namespace xuna {
// Backlight level for the T-Display-S3 LED driver: 16 steps, 16 = full (heat,
// 2026-10-24). Presentation only. Awake sits a little below full; a sleeping
// or resting XUNA dims the screen; cards show at full so text and the QR stay
// easy to read. Levels change one step at a time, so changes fade in.
struct BacklightLevels {
  uint8_t awake=12;   // 75 % LED current, looks ~90 % as bright
  uint8_t asleep=7;   // sleeping / resting
  uint8_t card=16;    // status / pairing cards
};
class BacklightDimmer {
 public:
  static constexpr uint8_t kMax=16;
  static constexpr uint32_t kStepMs=60;  // awake ↔ asleep fades in ~0.3 s
  explicit BacklightDimmer(BacklightLevels levels=BacklightLevels{}):levels_(levels){}
  uint8_t target(PetState state,bool cardOpen) const {
    const uint8_t level=cardOpen?levels_.card:state==PetState::Sleep?levels_.asleep:levels_.awake;
    return level<1?1:level>kMax?kMax:level;  // 0 would switch the driver off
  }
  // The level to show now. Cards jump straight to their level.
  uint8_t update(uint32_t now,PetState state,bool cardOpen){
    const uint8_t want=target(state,cardOpen);
    if(cardOpen||level_==want){level_=want;steppedAt_=now;return level_;}
    if(uint32_t(now-steppedAt_)<kStepMs)return level_;
    steppedAt_=now;
    level_=level_<want?uint8_t(level_+1):uint8_t(level_-1);
    return level_;
  }
  uint8_t level() const {return level_;}
 private:
  BacklightLevels levels_;
  uint8_t level_=kMax;  // the driver powers up at full
  uint32_t steppedAt_=0;
};
}
