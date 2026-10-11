#include "DisplayManager.h"
static_assert(USER_SETUP_ID == 206, "Use the standard T-Display-S3 LCD setup");
static_assert(TFT_WIDTH == 170 && TFT_HEIGHT == 320, "Unexpected LCD geometry");
static_assert(TFT_CS == 6 && TFT_DC == 7 && TFT_RST == 5 && TFT_WR == 8 && TFT_RD == 9,
              "Unexpected LCD control pins");
static_assert(xuna::kRotation <= 3, "Rotation must be 0..3");
namespace xuna {
namespace {
constexpr int kSpriteSize = 80;
constexpr uint16_t kBackground = 0x0843;
// Original 16x16 XUNA placeholder sprite; each cell is scaled to 4x4 pixels.
constexpr char kBody[16][17] = {
  "................", "...XX......XX...", "..XooX....XooX..",
  "..XoooXXXXoooX..", "..XooooooooooX..", ".XooooooooooooX.",
  ".XooooooooooooX.", ".XooooooooooooX.", ".XooooooooooooX.",
  ".XooooooooooooX.", ".XooooooooooooX.", "..XooooooooooX..",
  "...XooooooooX...", "....XXXXXXXX....", "....XX....XX....",
  "................"
};
}
bool DisplayManager::begin() {
  pinMode(kPowerPin, OUTPUT); digitalWrite(kPowerPin, HIGH);
  pinMode(kBacklightPin, OUTPUT); digitalWrite(kBacklightPin, LOW);
  lcd_.init(); // TFT_eSPI cannot report physical LCD presence/init failure.
  lcd_.setRotation(kRotation);
  sprite_.setColorDepth(16);
  if (!sprite_.createSprite(kSpriteSize, kSpriteSize)) return false;
  lcd_.fillScreen(kBackground);
  lcd_.setTextColor(TFT_WHITE, kBackground);
  lcd_.setTextSize(2); lcd_.setCursor(12, 12); lcd_.print("XUNA");
  lcd_.setTextSize(1);
  lcd_.setCursor(12, lcd_.height() - 14);
  lcd_.print("BOOT: happy  |  GPIO14: idle");
  digitalWrite(kBacklightPin, HIGH);
  ready_ = true; return true;
}
void DisplayManager::render(PetState state, uint8_t frame) {
  if (!ready_) return;
  sprite_.fillSprite(kBackground);
  const bool happy = state == PetState::Happy;
  const int y = 6 + (frame ? 0 : 4);
  const uint16_t body = happy ? 0xFEAC : 0xB6FF;
  for (int row = 0; row < 16; ++row) {
    for (int col = 0; col < 16; ++col) {
      const char cell = kBody[row][col];
      if (cell != '.') sprite_.fillRect(8 + col * 4, y + row * 4, 4, 4,
                                       cell == 'X' ? TFT_WHITE : body);
    }
  }
  const int eyeY = y + 28;
  if (happy || frame) {
    sprite_.fillRect(24, eyeY + 4, 12, 4, TFT_BLACK);
    sprite_.fillRect(48, eyeY + 4, 12, 4, TFT_BLACK);
    if (happy) {
      sprite_.fillRect(28, eyeY, 4, 4, TFT_BLACK);
      sprite_.fillRect(52, eyeY, 4, 4, TFT_BLACK);
    }
  } else {
    sprite_.fillRect(28, eyeY, 4, 8, TFT_BLACK);
    sprite_.fillRect(52, eyeY, 4, 8, TFT_BLACK);
  }
  sprite_.fillRect(36, y + 44, 12, 4, TFT_BLACK);
  if (happy) sprite_.fillRect(40, y + 48, 4, 4, TFT_BLACK);
  sprite_.pushSprite((lcd_.width() - kSpriteSize) / 2,
                     (lcd_.height() - kSpriteSize) / 2);
}
}
