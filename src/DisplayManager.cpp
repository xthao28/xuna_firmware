#include "BoardConfig.h"
#include "DisplayManager.h"
#include "assets/XunaAnimatedScene.h"
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
namespace xuna {
bool DisplayManager::begin() {
  pinMode(kPowerPin, OUTPUT); digitalWrite(kPowerPin, HIGH);
  pinMode(kBacklightPin, OUTPUT); digitalWrite(kBacklightPin, LOW);
  lcd_.init();lcd_.setRotation(kRotation);
  if(lcd_.width()!=scene::kWidth||lcd_.height()!=scene::kHeight)return false;
  lcd_.setSwapBytes(true);
  lcd_.pushImage(0,0,scene::kWidth,scene::kHeight,scene::kBackground);
  region_=static_cast<uint16_t*>(malloc(scene::kSpriteWidth*scene::kSpriteHeight*2));
  if(!region_){digitalWrite(kBacklightPin,HIGH);return false;}
  random_=Pcg32(esp_random(),54);const uint32_t now=millis();visual_.begin(now,random_);
  update(now,PetState::Idle);digitalWrite(kBacklightPin,HIGH);
  Serial.printf("scene=animated region_bytes=%u flash_pixels=%u\n",scene::kSpriteWidth*scene::kSpriteHeight*2,
                unsigned(sizeof(scene::kBackground)+sizeof(scene::kFrames)));
  return true;
}
void DisplayManager::update(uint32_t now,PetState state){
  if(!region_)return;
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
}
