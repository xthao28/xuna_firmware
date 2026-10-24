#pragma once
#include <TFT_eSPI.h>
#include <xuna/visual/PetVisualAnimation.h>
#include <xuna/visual/DeviceCards.h>
#include <xuna/visual/SceneEffects.h>
namespace xuna {
class DisplayManager {
 public:
  bool begin();
  void update(uint32_t now,PetState state);
  // Window scene (redrawn only on change) and the effect above XUNA's head.
  void setBackdrop(Backdrop backdrop){backdrop_=backdrop;}
  void showEffect(EffectKind kind,uint32_t now){effect_.start(kind,now);}
  // Pet sprite frame period (personality tempo, PE-2).
  void setFramePeriod(uint16_t ms,uint32_t now){visual_.setInterval(ms,now);}
  // Full-screen cards over the scene (PSRAM framebuffer, allocated on first
  // use). Any button closes them; they also time out. False if unavailable.
  bool showStatus(const StatusCardModel& model,uint32_t now);
  bool showPairing(const char* uri,const char* shortId,uint32_t now);
  bool cardOpen() const {return cardUntil_!=0;}
  void closeCard();
  uint32_t draws() const {return draws_;}
  uint32_t maxDrawUs() const {return maxDrawUs_;}
  uint32_t maxSceneUs() const {return maxSceneUs_;}
 private:
  void drawWindow();
  void drawEffect(const EffectSample& sample);
  bool ensureCardBuffers();
  void openCard(uint32_t now,uint32_t durationMs);
  TFT_eSPI lcd_;
  uint16_t* region_=nullptr; // allocated once, fixed full sprite canvas
  uint16_t* window_=nullptr; // allocated once, window frame + glass
  uint16_t* effect_buf_=nullptr; // allocated once, effect region
  Pcg32 random_{42,54};PetVisualAnimation visual_;
  WindowWeather weather_;ReactionEffect effect_;
  Backdrop backdrop_=Backdrop::Day,drawnBackdrop_=Backdrop::Day;
  bool windowValid_=true;
  uint16_t* card_=nullptr; // PSRAM, full screen
  uint8_t* qr_=nullptr;    // PSRAM, qrcodegen symbol + scratch
  uint32_t cardAt_=0,cardUntil_=0; // cardUntil_ = duration; 0 = closed
  EffectSample drawnEffect_{EffectKind::None,0,false};
  uint8_t lastFrame_=255;
  uint32_t draws_=0,maxDrawUs_=0,maxSceneUs_=0;
};
}
