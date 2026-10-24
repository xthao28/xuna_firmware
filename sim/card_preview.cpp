// Host preview of the device cards: renders the status and pairing cards with
// the real fonts and xuna_core code into binary PPM files (320 × 170).
//   cc -std=c99 -c lib/qrcodegen/src/qrcodegen.c &&
//   c++ -std=c++11 -Wall -Wextra -Werror -Ilib/xuna_core/include -Iinclude \
//     -Ilib/qrcodegen/src sim/card_preview.cpp src/assets/XunaUiFont.cpp qrcodegen.o \
//     -o card_preview && ./card_preview out_dir
#include <cstdio>
#include <cstring>
#include <string>

#include <qrcodegen.h>
#include <xuna/visual/DeviceCards.h>

#include "assets/XunaUiFont.h"

namespace {
uint16_t pixels[320 * 170];

bool writePpm(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n320 170\n255\n");
  for (uint16_t p : pixels) {
    const unsigned char rgb[3] = {static_cast<unsigned char>(((p >> 11) & 31) * 255 / 31),
                                  static_cast<unsigned char>(((p >> 5) & 63) * 255 / 63),
                                  static_cast<unsigned char>((p & 31) * 255 / 31)};
    std::fwrite(rgb, 1, 3, f);
  }
  return std::fclose(f) == 0;
}

bool qrDark(const void* ctx, int x, int y) {
  return qrcodegen_getModule(static_cast<const uint8_t*>(ctx), x, y);
}
}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  xuna::Canvas canvas{pixels, 320, 170};
  const xuna::CardFonts fonts{xuna::ui::kFontTitle, xuna::ui::kFontValue, xuna::ui::kFontLabel};

  xuna::StatusCardModel m;
  m.activeSeconds = 3 * 86400 + 5 * 3600 + 120;
  m.energyPct = 72;
  m.state = xuna::PetState::Happy;
  m.quiet = xuna::ActionId::Happy;
  m.trait = xuna::TraitWord::Unhurried;  // the widest trait word
  m.greeting = xuna::ActionId::Idle;
  m.feedbackCount = 12;
  m.arrivalKnown = true;
  m.arrivalHour = 19;
  m.arrivalMinute = 10;
  std::strcpy(m.shortId, "d945c770");
  m.mood = xuna::MoodWord::Bored;  // the widest mood word
  xuna::renderStatusCard(canvas, fonts, m);
  if (!writePpm(dir + "/status_card.ppm")) return 1;

  const uint8_t petId[16] = {0xd9, 0x45, 0xc7, 0x70, 0x26, 0xfd, 0x4f, 0x4a,
                             0x8a, 0x89, 0x9b, 0xff, 0x4a, 0xee, 0xd8, 0x6d};
  uint8_t key[65] = {0x04, 0xbb, 0xbe, 0x60, 0xb5, 0x2d, 0xb8, 0x08, 0x9e};
  char uri[xuna::kPairingUriSize];
  if (!xuna::pairingUri(petId, key, uri, sizeof(uri))) return 1;
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
  if (!qrcodegen_encodeText(uri, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true))
    return 1;
  xuna::renderPairingCard(canvas, fonts, "d945c770", qrcodegen_getSize(qr), qrDark, qr);
  if (!writePpm(dir + "/pairing_card.ppm")) return 1;
  std::printf("uri=%s qr_size=%d\n", uri, qrcodegen_getSize(qr));
  return 0;
}
