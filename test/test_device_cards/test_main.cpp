#include <cstring>

#include <unity.h>

#include <qrcodegen.h>
#include <xuna/ButtonPad.h>
#include <xuna/visual/DeviceCards.h>

using namespace xuna;

namespace {
// Fake font: 'A' is a 2 × 2 full block above the baseline; 'đ' a 1 × 1 half dot.
const uint8_t kAlpha[] = {255, 255, 255, 255, 128};
const UiGlyph kGlyphs[] = {{' ', 0, 0, 0, 0, 0, 3},
                           {'A', 0, 2, 2, 0, -2, 3},
                           {0x0111, 4, 1, 1, 0, -1, 2}};
const UiFont kFont = {kGlyphs, 3, kAlpha, 2, 3};
uint16_t pixels[320 * 170];
}  // namespace

void utf8_decodes_vietnamese_and_survives_bad_bytes() {
  const char* p = "ađố";
  TEST_ASSERT_EQUAL_UINT32('a', decodeUtf8(p));
  TEST_ASSERT_EQUAL_UINT32(0x0111, decodeUtf8(p));
  TEST_ASSERT_EQUAL_UINT32(0x1ED1, decodeUtf8(p));
  TEST_ASSERT_EQUAL_UINT32(0, decodeUtf8(p));
  const char bad[] = {char(0xC3), 'x', char(0xF8), char(0x80), 0};
  p = bad;
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, decodeUtf8(p));  // truncated sequence
  TEST_ASSERT_EQUAL_UINT32('x', decodeUtf8(p));
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, decodeUtf8(p));  // invalid lead byte
  TEST_ASSERT_EQUAL_UINT32(0xFFFD, decodeUtf8(p));  // stray continuation
  TEST_ASSERT_EQUAL_UINT32(0, decodeUtf8(p));
}

void text_measures_draws_blends_and_clips() {
  TEST_ASSERT_EQUAL_UINT16(3 + 2 + 3, textWidth(kFont, "AđZ"));  // missing Z = space
  Canvas c{pixels, 4, 3};
  fillRect(c, -5, -5, 100, 100, 0x0000);
  const int end = drawText(c, kFont, 0, 2, "Ađ", 0xFFFF);
  TEST_ASSERT_EQUAL_INT(5, end);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, pixels[0]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, pixels[1 * 4 + 1]);
  TEST_ASSERT_EQUAL_HEX16(0x0000, pixels[2 * 4 + 0]);
  TEST_ASSERT_EQUAL_HEX16(blend565(0x0000, 0xFFFF, 128), pixels[1 * 4 + 3]);
  // Fully outside or overlapping the edges: nothing written out of bounds.
  drawText(c, kFont, -10, -10, "AAAA", 0x1234);
  drawText(c, kFont, 3, 200, "AAAA", 0x1234);
  drawText(c, kFont, 3, 1, "AAAA", 0x1234);
  TEST_ASSERT_EQUAL_HEX16(0x1234, pixels[0 * 4 + 3]);
  TEST_ASSERT_EQUAL_HEX16(0xF800, blend565(0x0000, 0xF800, 255));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, blend565(0x07E0, 0xF800, 0));
}

void lifetime_and_pairing_text() {
  char out[24];
  formatLifetime(45 * 60 + 59, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("45 phút", out);
  formatLifetime(3 * 3600 + 5 * 60, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("3 giờ 5 phút", out);
  formatLifetime(4 * 86400 + 2 * 3600, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("4 ngày 2 giờ", out);

  uint8_t pet[16];
  for (uint8_t i = 0; i < 16; ++i) pet[i] = uint8_t(i * 17);
  uint8_t key[65] = {0x04, 0xab, 0xcd, 1, 2, 3, 4, 5, 6, 0xff};
  char uri[kPairingUriSize];
  TEST_ASSERT_FALSE(pairingUri(pet, key, uri, sizeof(uri) - 1));
  TEST_ASSERT_FALSE(pairingUri(nullptr, key, uri, sizeof(uri)));
  TEST_ASSERT_TRUE(pairingUri(pet, key, uri, sizeof(uri)));
  TEST_ASSERT_EQUAL_STRING(
      "xuna://pair?pet=00112233445566778899aabbccddeeff&key=abcd010203040506", uri);
  // The payload fits a version 5 symbol at ECC M (37 modules, 4 px each on 170 px).
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
  TEST_ASSERT_TRUE(qrcodegen_encodeText(uri, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 5,
                                        qrcodegen_Mask_AUTO, true));
  TEST_ASSERT_EQUAL_INT(37, qrcodegen_getSize(qr));
}

void status_card_from_a_fresh_core_and_render_bounds() {
  LifeCore core;
  core.begin(MonoMs(0), false);
  const uint8_t pet[16] = {0xd9, 0x45, 0xc7, 0x70};
  const StatusCardModel m = makeStatusCard(core, 32768, PetState::Curious, pet);
  TEST_ASSERT_EQUAL_UINT8(50, m.energyPct);
  TEST_ASSERT_FALSE(m.arrivalKnown);   // no routine learned yet
  TEST_ASSERT_FALSE(m.weekend);        // no trusted time: weekday wording
  TEST_ASSERT_EQUAL_STRING("d945c770", m.shortId);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PetState::Curious), uint8_t(m.state));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(MoodWord::Calm), uint8_t(m.mood));  // neutral, unseeded
  TEST_ASSERT_EQUAL_STRING("", makeStatusCard(core, 0, PetState::Idle, nullptr).shortId);

  Canvas c{pixels, 320, 170};
  StatusCardModel full = m;
  full.energyPct = 100;
  full.activeSeconds = 0xFFFFFFFFu;
  full.feedbackCount = 0xFFFFFFFFu;
  renderStatusCard(c, CardFonts{kFont, kFont, kFont}, full);  // ASan guards bounds
  TEST_ASSERT_EQUAL_HEX16(card::kBackground, pixels[0]);
  TEST_ASSERT_EQUAL_STRING("tò mò", actionName(ActionId::Curious));
  TEST_ASSERT_EQUAL_STRING("đang ngủ", stateName(PetState::Sleep));
  TEST_ASSERT_EQUAL_STRING("hơi buồn", moodName(MoodWord::Low));
  TEST_ASSERT_EQUAL_STRING("bình yên", moodName(MoodWord::Calm));
}

bool checker(const void*, int x, int y) { return (x + y) % 2 == 0; }

void pairing_card_scales_the_symbol_inside_the_screen() {
  Canvas c{pixels, 320, 170};
  renderPairingCard(c, CardFonts{kFont, kFont, kFont}, "d945c770", 37, checker, nullptr);
  // 41 modules × 4 px = 164 px, centred vertically at x = 8.
  TEST_ASSERT_EQUAL_HEX16(card::kQrLight, pixels[3 * 320 + 8]);       // quiet zone
  TEST_ASSERT_EQUAL_HEX16(card::kQrDark, pixels[(3 + 8) * 320 + 8 + 8]);  // module (0,0)
  TEST_ASSERT_EQUAL_HEX16(card::kBackground, pixels[2 * 320 + 8]);    // above the symbol
  renderPairingCard(c, CardFonts{kFont, kFont, kFont}, nullptr, 177, checker, nullptr);  // v40
}

void pad_short_hold_and_combo() {
  ButtonPad pad;
  PadEvents e = pad.update(true, false, 0);
  e = pad.update(false, false, 100);
  TEST_ASSERT_TRUE(e.a.shortPress);
  pad.update(false, true, 200);
  e = pad.update(false, true, 200 + HoldButton::kHoldMs);
  TEST_ASSERT_TRUE(e.b.hold);
  pad.update(false, false, 1500);

  // Both down (100 ms apart): no single events, one combo after 3 s.
  pad.update(true, false, 2000);
  e = pad.update(true, true, 2100);
  TEST_ASSERT_FALSE(e.a.shortPress || e.a.hold || e.b.shortPress || e.b.hold);
  e = pad.update(true, true, 2100 + HoldButton::kHoldMs);
  TEST_ASSERT_FALSE(e.a.hold || e.b.hold || e.comboHold);
  e = pad.update(true, true, 2100 + ButtonPad::kComboHoldMs);
  TEST_ASSERT_TRUE(e.comboHold);
  TEST_ASSERT_FALSE(pad.update(true, true, 9000).comboHold);  // once
  e = pad.update(false, true, 9100);  // one released: still swallowed
  TEST_ASSERT_FALSE(e.a.shortPress || e.b.hold);
  e = pad.update(false, false, 9200);
  TEST_ASSERT_FALSE(e.a.shortPress || e.b.shortPress);
  // An aborted combo (released before 3 s) does nothing at all.
  pad.update(true, true, 10000);
  e = pad.update(false, false, 10500);
  TEST_ASSERT_FALSE(e.a.shortPress || e.b.shortPress || e.comboHold);
  // Normal presses work again afterwards.
  pad.update(true, false, 11000);
  TEST_ASSERT_TRUE(pad.update(false, false, 11100).a.shortPress);
}

void pad_ignores_buttons_held_at_startup() {
  ButtonPad pad;
  pad.begin(true, false);
  TEST_ASSERT_FALSE(pad.update(true, false, 5000).a.hold);
  TEST_ASSERT_FALSE(pad.update(false, false, 5100).a.shortPress);
  pad.update(true, false, 6000);
  TEST_ASSERT_TRUE(pad.update(false, false, 6100).a.shortPress);
  ButtonPad both;
  both.begin(true, true);
  TEST_ASSERT_FALSE(both.update(true, true, 10000).comboHold);
  both.update(false, false, 10100);
  both.update(true, true, 11000);
  TEST_ASSERT_TRUE(both.update(true, true, 11000 + ButtonPad::kComboHoldMs).comboHold);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(utf8_decodes_vietnamese_and_survives_bad_bytes);
  RUN_TEST(text_measures_draws_blends_and_clips);
  RUN_TEST(lifetime_and_pairing_text);
  RUN_TEST(status_card_from_a_fresh_core_and_render_bounds);
  RUN_TEST(pairing_card_scales_the_symbol_inside_the_screen);
  RUN_TEST(pad_short_hold_and_combo);
  RUN_TEST(pad_ignores_buttons_held_at_startup);
  return UNITY_END();
}
