#include "BoardConfig.h"
#include "DisplayManager.h"
#include "assets/XunaAnimatedScene.h"
#include "assets/XunaUiFont.h"
#include <esp_heap_caps.h>
#include <qrcodegen.h>
static_assert(USER_SETUP_ID == 206, "Use the standard T-Display-S3 LCD setup");
static_assert(TFT_WIDTH == 170 && TFT_HEIGHT == 320, "Unexpected LCD geometry");
static_assert(TFT_CS == 6 && TFT_DC == 7 && TFT_RST == 5 && TFT_WR == 8 && TFT_RD == 9,
              "Unexpected LCD control pins");
static_assert(TFT_D0 == 39 && TFT_D1 == 40 && TFT_D2 == 41 && TFT_D3 == 42 &&
              TFT_D4 == 45 && TFT_D5 == 46 && TFT_D6 == 47 && TFT_D7 == 48,
              "Unexpected LCD data pins");
static_assert(TFT_RGB_ORDER == TFT_RGB, "Asset requires the existing RGB panel configuration");
static_assert(xuna::kRotation == 1, "Animated landscape asset expects rotation 1");
static_assert(xuna::scene::kWidth == 320 && xuna::scene::kHeight == 170,
              "Regenerate the scene for the configured LCD orientation");
// Window/effect regions push on their own, so they must never overlap the pet canvas.
static_assert(xuna::scene::kWindowX + xuna::scene::kWindowW <= xuna::scene::kAnchorX,
              "Window must stay left of the pet canvas");
static_assert(xuna::scene::kEffectY + xuna::scene::kEffectH <= xuna::scene::kAnchorY &&
              xuna::scene::kEffectX >= xuna::scene::kWindowX + xuna::scene::kWindowW,
              "Effect region must stay above the pet and clear of the window");
static_assert(xuna::scene::kEffectH - xuna::scene::kEffectSpriteH >= xuna::ReactionEffect::kMaxRise &&
              xuna::scene::kEffectW >= xuna::scene::kEffectSpriteW,
              "Effect sprite must fit its region at full rise");
static_assert(xuna::scene::kWindowCount == 4 && xuna::scene::kEffectCount == 3,
              "Asset order must match Backdrop and EffectKind");
static_assert(xuna::scene::kGlassW <= 255 && xuna::scene::kGlassH <= 255, "Glass fits WindowWeather");
namespace xuna {
bool DisplayManager::begin() {
  pinMode(kPowerPin, OUTPUT); digitalWrite(kPowerPin, HIGH);
  pinMode(kBacklightPin, OUTPUT); digitalWrite(kBacklightPin, LOW);
  lcd_.init();lcd_.setRotation(kRotation);
  if(lcd_.width()!=scene::kWidth||lcd_.height()!=scene::kHeight)return false;
  lcd_.setSwapBytes(true);
  lcd_.pushImage(0,0,scene::kWidth,scene::kHeight,scene::kBackground);
  region_=static_cast<uint16_t*>(malloc(scene::kSpriteWidth*scene::kSpriteHeight*2));
  window_=static_cast<uint16_t*>(malloc(scene::kWindowW*scene::kWindowH*2));
  effect_buf_=static_cast<uint16_t*>(malloc(scene::kEffectW*scene::kEffectH*2));
  if(!region_||!window_||!effect_buf_){digitalWrite(kBacklightPin,HIGH);return false;}
  random_=Pcg32(esp_random(),54);const uint32_t now=millis();visual_.begin(now,random_);
  weather_.begin(scene::kGlassW,scene::kGlassH,now,random_);
  update(now,PetState::Idle);digitalWrite(kBacklightPin,HIGH);
  Serial.printf("scene=animated region_bytes=%u window_bytes=%u effect_bytes=%u flash_pixels=%u\n",
                scene::kSpriteWidth*scene::kSpriteHeight*2,scene::kWindowW*scene::kWindowH*2,
                scene::kEffectW*scene::kEffectH*2,
                unsigned(sizeof(scene::kBackground)+sizeof(scene::kFrames)+sizeof(scene::kWindows)+sizeof(scene::kEffects)));
  return true;
}
void DisplayManager::update(uint32_t now,PetState state){
  if(!region_)return;
  if(cardOpen()){
    if(uint32_t(now-cardAt_)>=cardUntil_)closeCard();
    return; // nothing else draws over a card
  }
  // Window and effect regions never overlap the pet canvas, so each pushes alone.
  const uint32_t sceneStart=micros();bool sceneDrawn=false;
  bool windowDirty=!windowValid_||backdrop_!=drawnBackdrop_;
  if(backdrop_==Backdrop::Rain&&weather_.stepRain(now,random_))windowDirty=true;
  if(backdrop_==Backdrop::Night&&weather_.stepStars(now,scene::kStarCount,random_))windowDirty=true;
  if(windowDirty){drawWindow();sceneDrawn=true;}
  const EffectSample sample=effect_.sample(now);
  if(sample!=drawnEffect_){drawEffect(sample);sceneDrawn=true;}
  if(sceneDrawn){const uint32_t elapsed=micros()-sceneStart;if(elapsed>maxSceneUs_)maxSceneUs_=elapsed;}
  const uint8_t frame=visual_.frame(now,state,random_);if(frame==lastFrame_)return;
  const uint32_t start=micros();
  const uint16_t* pixels=scene::kFrames+frame*scene::kSpriteWidth*scene::kSpriteHeight;
  // Every frame uses the same full canvas: union of previous/current bounding boxes.
  // Restore/composite offscreen, then push one opaque region (no erase-then-draw flicker).
  for(unsigned y=0;y<scene::kSpriteHeight;++y)for(unsigned x=0;x<scene::kSpriteWidth;++x){
    const unsigned i=y*scene::kSpriteWidth+x;const uint16_t color=pgm_read_word(pixels+i);
    region_[i]=color==scene::kTransparent?pgm_read_word(scene::kBackground+(scene::kAnchorY+y)*scene::kWidth+scene::kAnchorX+x):color;
  }
  lcd_.pushImage(scene::kAnchorX,scene::kAnchorY,scene::kSpriteWidth,scene::kSpriteHeight,region_);
  lastFrame_=frame;++draws_;const uint32_t elapsed=micros()-start;if(elapsed>maxDrawUs_)maxDrawUs_=elapsed;
}
void DisplayManager::drawWindow(){
  const uint16_t* src=scene::kWindows+unsigned(backdrop_)*scene::kWindowW*scene::kWindowH;
  for(unsigned i=0;i<unsigned(scene::kWindowW)*scene::kWindowH;++i)window_[i]=pgm_read_word(src+i);
  auto put=[this](int x,int y,uint16_t color){ // screen coordinates, glass only
    if(x<scene::kGlassX||y<scene::kGlassY||x>=scene::kGlassX+scene::kGlassW||y>=scene::kGlassY+scene::kGlassH)return;
    if(x>=scene::kMullionX0&&x<=scene::kMullionX1)return;
    window_[(y-scene::kWindowY)*scene::kWindowW+(x-scene::kWindowX)]=color;
  };
  if(backdrop_==Backdrop::Rain){
    const RainDrop* drops=weather_.drops();
    for(uint8_t i=0;i<WindowWeather::kDrops;++i)for(int k=0;k<3;++k)
      put(scene::kGlassX+drops[i].x+k,scene::kGlassY+drops[i].y-2*k,scene::kRainColor); // slanted streak
  }
  if(backdrop_==Backdrop::Night){
    for(uint8_t i=0;i<scene::kStarCount;++i){
      const bool bright=i>=8||(weather_.stars()&(1u<<i));
      put(pgm_read_byte(scene::kStarX+i),pgm_read_byte(scene::kStarY+i),bright?scene::kStarBright:scene::kStarDim);
    }
  }
  lcd_.pushImage(scene::kWindowX,scene::kWindowY,scene::kWindowW,scene::kWindowH,window_);
  drawnBackdrop_=backdrop_;windowValid_=true;
}
void DisplayManager::drawEffect(const EffectSample& sample){
  for(unsigned y=0;y<scene::kEffectH;++y)for(unsigned x=0;x<scene::kEffectW;++x)
    effect_buf_[y*scene::kEffectW+x]=pgm_read_word(scene::kBackground+(scene::kEffectY+y)*scene::kWidth+scene::kEffectX+x);
  if(sample.kind!=EffectKind::None&&sample.visible){
    const uint16_t* sprite=scene::kEffects+(unsigned(sample.kind)-1)*scene::kEffectSpriteW*scene::kEffectSpriteH;
    const unsigned ox=(scene::kEffectW-scene::kEffectSpriteW)/2,oy=scene::kEffectH-scene::kEffectSpriteH-sample.rise;
    for(unsigned y=0;y<scene::kEffectSpriteH;++y)for(unsigned x=0;x<scene::kEffectSpriteW;++x){
      const uint16_t color=pgm_read_word(sprite+y*scene::kEffectSpriteW+x);
      if(color!=scene::kTransparent)effect_buf_[(oy+y)*scene::kEffectW+ox+x]=color;
    }
  }
  lcd_.pushImage(scene::kEffectX,scene::kEffectY,scene::kEffectW,scene::kEffectH,effect_buf_);
  drawnEffect_=sample;
}
namespace {
const CardFonts kCardFonts{ui::kFontTitle,ui::kFontValue,ui::kFontLabel};
bool qrDark(const void* ctx,int x,int y){return qrcodegen_getModule(static_cast<const uint8_t*>(ctx),x,y);}
}
bool DisplayManager::ensureCardBuffers(){
  if(!card_)card_=static_cast<uint16_t*>(heap_caps_malloc(scene::kWidth*scene::kHeight*2,MALLOC_CAP_SPIRAM));
  if(!qr_)qr_=static_cast<uint8_t*>(heap_caps_malloc(2*qrcodegen_BUFFER_LEN_MAX,MALLOC_CAP_SPIRAM));
  return card_&&qr_;
}
void DisplayManager::openCard(uint32_t now,uint32_t durationMs){
  const uint32_t start=micros();
  lcd_.pushImage(0,0,scene::kWidth,scene::kHeight,card_);
  const uint32_t elapsed=micros()-start;if(elapsed>maxSceneUs_)maxSceneUs_=elapsed;
  cardAt_=now;cardUntil_=durationMs;
}
bool DisplayManager::showStatus(const StatusCardModel& model,uint32_t now){
  if(!region_||!ensureCardBuffers())return false;
  Canvas canvas{card_,scene::kWidth,scene::kHeight};
  renderStatusCard(canvas,kCardFonts,model);
  openCard(now,8000);
  return true;
}
bool DisplayManager::showPairing(const char* uri,const char* shortId,uint32_t now){
  if(!region_||!ensureCardBuffers())return false;
  uint8_t* symbol=qr_;uint8_t* scratch=qr_+qrcodegen_BUFFER_LEN_MAX;
  if(!qrcodegen_encodeText(uri,scratch,symbol,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,true))return false;
  Canvas canvas{card_,scene::kWidth,scene::kHeight};
  renderPairingCard(canvas,kCardFonts,shortId,qrcodegen_getSize(symbol),qrDark,symbol);
  openCard(now,60000);
  return true;
}
// Restores the whole scene: background, current window, effect, pet.
void DisplayManager::closeCard(){
  if(!cardOpen())return;
  cardUntil_=0;
  lcd_.pushImage(0,0,scene::kWidth,scene::kHeight,scene::kBackground);
  windowValid_=false;lastFrame_=255;
  drawnEffect_=EffectSample{EffectKind::None,0,false};
}
}
