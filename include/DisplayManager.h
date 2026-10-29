#pragma once
#include <TFT_eSPI.h>
#include <xuna/visual/PetVisualAnimation.h>
#include <xuna/visual/DeviceCards.h>
#include <xuna/visual/SpeechBubble.h>
#include <xuna/visual/SceneEffects.h>
#include <xuna/visual/VariantPlayer.h>
#include <xuna/visual/Backlight.h>
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
  uint16_t framePeriod() const {return visual_.interval();}
  void setBrightnessPreset(BrightnessPreset preset){backlight_.setPreset(preset);}
  BrightnessPreset brightnessPreset() const {return backlight_.preset();}
  void setScreenPower(ScreenPowerState power,uint32_t now,PetState state);
  void wake(uint32_t now,PetState state){setScreenPower(ScreenPowerState::Active,now,state);}
  bool screenSleeping() const {return screenSleeping_;}
  ScreenPowerState screenPower() const {return powerState_;}
  bool overlayOpen(uint32_t now) const {
    return cardOpen()||speech_.page(now)>=0||drawnSpeechPage_>=0;
  }
  // True while a card, speech bubble, Track A variant or reaction effect is
  // active (used by PowerPacer so a backlight fade into Dim does not cancel Dim).
  bool overlayOrActionBusy(uint32_t now) const {
    return overlayOpen(now)||variant_.playing()!=VisualVariant::None||
           effect_.active()||drawnEffect_.kind!=EffectKind::None;
  }
  // True while any transient visual or backlight fade needs 10 ms pacing.
  bool visualBusy(uint32_t now,PetState state) const {
    if(screenSleeping_)return false;
    const bool overlay=cardOpen()||speech_.page(now)>=0;
    const bool dimmed=powerState_==ScreenPowerState::Dim;
    return overlayOrActionBusy(now)||backlight_.fading(state,overlay,dimmed);
  }
  // Track A variant over the current pose (LifeCore::takeVariant).
  void playVariant(VisualVariant v,uint32_t now){variant_.start(v,now);}
  // Full-screen cards over the scene (PSRAM framebuffer, allocated on first
  // use). Any button closes them; they also time out. False if unavailable.
  bool showStatus(const StatusCardModel& model,uint32_t now);
  bool showPairing(const char* uri,const char* shortId,uint32_t now);
  bool cardOpen() const {return cardUntil_!=0;}
  void closeCard();
  // Speech subtitle above XUNA (owner 2026-10-27). It replaces the previous
  // one, pages by itself and hides after its last page; a card covers it
  // meanwhile. Effects above XUNA's head are hidden while it shows.
  bool showSpeech(const char* text,uint32_t now);
  void clearSpeech(){speech_.clear();}
  uint32_t draws() const {return draws_;}
  uint32_t maxDrawUs() const {return maxDrawUs_;}
  uint32_t maxSceneUs() const {return maxSceneUs_;}
  uint8_t backlight() const {return backlightHw_;}
 private:
  void drawWindow();
  void drawEffect(const EffectSample& sample);
  void drawSpeech(int page);
  bool ensureCardBuffers();
  void openCard(uint32_t now,uint32_t durationMs);
  void setBacklight(uint8_t level);
  TFT_eSPI lcd_;
  uint16_t* region_=nullptr; // allocated once, fixed full sprite canvas
  uint16_t* window_=nullptr; // allocated once, window frame + glass
  uint16_t* effect_buf_=nullptr; // allocated once, effect region
  Pcg32 random_{42,54};PetVisualAnimation visual_;VariantPlayer variant_;
  WindowWeather weather_;ReactionEffect effect_;
  Backdrop backdrop_=Backdrop::Day,drawnBackdrop_=Backdrop::Day;
  bool windowValid_=true;
  uint16_t* card_=nullptr; // PSRAM, full screen
  uint8_t* qr_=nullptr;    // PSRAM, qrcodegen symbol + scratch
  uint32_t cardAt_=0,cardUntil_=0; // cardUntil_ = duration; 0 = closed
  SpeechBubble speech_;
  uint16_t* speechBuf_=nullptr; // PSRAM, bubble region, allocated on first use
  int drawnSpeechPage_=-1;
  bool effectValid_=true;
  EffectSample drawnEffect_{EffectKind::None,0,false};
  uint8_t lastFrame_=255;
  uint32_t draws_=0,maxDrawUs_=0,maxSceneUs_=0;
  BacklightDimmer backlight_;
  uint8_t backlightHw_=BacklightDimmer::kMax; // driver level after power-up
  ScreenPowerState powerState_=ScreenPowerState::Active;
  bool screenSleeping_=false;
};
}
