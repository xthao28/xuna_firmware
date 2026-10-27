#include <cstring>

#include <unity.h>

#include <xuna/visual/SpeechBubble.h>

using namespace xuna;

namespace {
// Fake font: 'A' is a 2 × 2 block (advance 3), 'đ' a 1 × 1 dot (advance 2),
// space advances 3; ascent 2, line height 3.
const uint8_t kAlpha[] = {255, 255, 255, 255, 255};
const UiGlyph kGlyphs[] = {{' ', 0, 0, 0, 0, 0, 3},
                           {'A', 0, 2, 2, 0, -2, 3},
                           {0x0111, 4, 1, 1, 0, -1, 2}};
const UiFont kFont = {kGlyphs, 3, kAlpha, 2, 3};
uint16_t pixels[60 * 20];

SpeechTextAssembler::Result feed(SpeechTextAssembler& a, uint8_t id, uint8_t index, uint8_t count,
                                 const char* text, size_t size = SIZE_MAX, uint8_t version = 1) {
  uint8_t frame[300] = {version, id, index, count};
  if (size == SIZE_MAX) size = std::strlen(text);
  std::memcpy(frame + 4, text, size);
  return a.feed(frame, 4 + size);
}
}  // namespace

void one_chunk_is_ready_and_cleaned() {
  SpeechTextAssembler a;
  TEST_ASSERT_EQUAL(int(SpeechTextAssembler::Result::Ready), int(feed(a, 1, 0, 1, "  Xin\n chào\t\tbạn  ")));
  TEST_ASSERT_EQUAL_STRING("Xin chào bạn", a.text());
  TEST_ASSERT_EQUAL(int(SpeechTextAssembler::Result::Ready), int(feed(a, 2, 0, 1, "Hello, I'm XUNA!")));
  TEST_ASSERT_EQUAL_STRING("Hello, I'm XUNA!", a.text());
}

void chunks_join_even_inside_a_code_point() {
  SpeechTextAssembler a;
  const char* text = "Lá đỏ";  // "đ" is C4 91: split between its two bytes
  const char* split = std::strchr(text, char(0xC4)) + 1;
  const size_t head = size_t(split - text);
  TEST_ASSERT_EQUAL(int(SpeechTextAssembler::Result::Partial), int(feed(a, 7, 0, 2, text, head)));
  TEST_ASSERT_EQUAL(int(SpeechTextAssembler::Result::Ready), int(feed(a, 7, 1, 2, split)));
  TEST_ASSERT_EQUAL_STRING("Lá đỏ", a.text());
}

void empty_or_blank_text_clears() {
  SpeechTextAssembler a;
  TEST_ASSERT_EQUAL(int(SpeechTextAssembler::Result::Cleared), int(feed(a, 1, 0, 1, "")));
  TEST_ASSERT_EQUAL(int(SpeechTextAssembler::Result::Cleared), int(feed(a, 2, 0, 1, " \n ")));
  TEST_ASSERT_EQUAL_STRING("", a.text());
}

void bad_frames_are_rejected_and_drop_the_unfinished_text() {
  using R = SpeechTextAssembler::Result;
  SpeechTextAssembler a;
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 1, 0, 1, "x", 1, 2)));  // version
  const uint8_t shortFrame[] = {1, 1, 0};
  TEST_ASSERT_EQUAL(int(R::Rejected), int(a.feed(shortFrame, sizeof(shortFrame))));
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 1, 0, 0, "x")));   // no chunks
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 1, 2, 2, "x")));   // index ≥ count
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 1, 0, 65, "x")));  // too many chunks
  uint8_t big[SpeechTextAssembler::kMaxFrame + 1] = {1, 1, 0, 1};
  TEST_ASSERT_EQUAL(int(R::Rejected), int(a.feed(big, sizeof(big))));
  // Out of order, another id or another count mid-text: the text is dropped.
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 3, 0, 3, "a")));
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 3, 2, 3, "c")));
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 3, 1, 3, "b")));
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 4, 0, 2, "a")));
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 5, 1, 2, "b")));
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 6, 0, 2, "a")));
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 6, 1, 3, "b")));
  // A new index 0 restarts cleanly after a partial text.
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 8, 0, 2, "old")));
  TEST_ASSERT_EQUAL(int(R::Ready), int(feed(a, 9, 0, 1, "new")));
  TEST_ASSERT_EQUAL_STRING("new", a.text());
}

void text_longer_than_the_limit_is_rejected() {
  using R = SpeechTextAssembler::Result;
  SpeechTextAssembler a;
  char chunk[201];
  std::memset(chunk, 'a', 200);
  chunk[200] = 0;
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 1, 0, 4, chunk)));
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 1, 1, 4, chunk)));
  TEST_ASSERT_EQUAL(int(R::Partial), int(feed(a, 1, 2, 4, chunk)));  // 600 bytes: still fits
  TEST_ASSERT_EQUAL(int(R::Rejected), int(feed(a, 1, 3, 4, "b")));
  TEST_ASSERT_EQUAL(int(R::Ready), int(feed(a, 2, 0, 1, "ok")));
}

void wrap_breaks_at_spaces_and_cuts_long_words() {
  TextLine lines[8];
  const char* text = "AA AA AAAA";
  TEST_ASSERT_EQUAL_UINT8(4, wrapText(kFont, text, 9, lines, 8));
  const uint16_t expected[4][2] = {{0, 2}, {3, 5}, {6, 9}, {9, 10}};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_UINT16(expected[i][0], lines[i].begin);
    TEST_ASSERT_EQUAL_UINT16(expected[i][1], lines[i].end);
  }
  // Multi-byte code points are measured and never split.
  const char* vi = "đđđđđ";
  TEST_ASSERT_EQUAL_UINT8(2, wrapText(kFont, vi, 8, lines, 8));
  TEST_ASSERT_EQUAL_UINT16(8, lines[0].end);  // four "đ" of 2 bytes, 8 px
  TEST_ASSERT_EQUAL_UINT16(8, lines[1].begin);
  // Too many lines: the rest is dropped.
  TEST_ASSERT_EQUAL_UINT8(2, wrapText(kFont, text, 9, lines, 2));
  TEST_ASSERT_EQUAL_UINT8(0, wrapText(kFont, "   ", 9, lines, 8));
}

void pages_last_long_enough_then_the_bubble_hides() {
  SpeechBubble b;
  TEST_ASSERT_EQUAL_INT(-1, b.page(0));
  b.show("AA", kFont, 9, 1000);
  TEST_ASSERT_EQUAL_UINT8(1, b.pageCount());
  TEST_ASSERT_EQUAL_INT(0, b.page(1000));
  TEST_ASSERT_EQUAL_INT(0, b.page(1000 + 2000 + 3000 - 1));  // 2 s minimum + 3 s hold
  TEST_ASSERT_EQUAL_INT(-1, b.page(1000 + 2000 + 3000));
  // Four lines: two pages; the first lasts its minimum, the second is held.
  b.show("AA AA AAAA", kFont, 9, 0xFFFFF000u);  // also across the millis() wrap
  TEST_ASSERT_EQUAL_UINT8(2, b.pageCount());
  TEST_ASSERT_EQUAL_UINT8(3, b.linesOn(0));
  TEST_ASSERT_EQUAL_UINT8(1, b.linesOn(1));
  TEST_ASSERT_EQUAL_INT(0, b.page(0xFFFFF000u + 1999));
  TEST_ASSERT_EQUAL_INT(1, b.page(0xFFFFF000u + 2000));
  TEST_ASSERT_EQUAL_INT(1, b.page(0xFFFFF000u + 6999));
  TEST_ASSERT_EQUAL_INT(-1, b.page(0xFFFFF000u + 7000));
  b.clear();
  TEST_ASSERT_EQUAL_INT(-1, b.page(0xFFFFF000u));
}

void a_long_page_is_capped_at_eight_seconds() {
  char text[201];
  std::memset(text, 'A', 200);
  text[200] = 0;
  SpeechBubble b;
  b.show(text, kFont, 3 * 60, 0);  // 60 per line, three lines on the first page
  TEST_ASSERT_EQUAL_INT(0, b.page(7999));
  TEST_ASSERT_EQUAL_INT(1, b.page(8000));
}

void bubble_draws_body_tail_and_text_inside_the_canvas() {
  for (uint16_t& p : pixels) p = 0x1234;
  Canvas c{pixels, 60, 20};
  SpeechBubble b;
  b.show("A", kFont, 40, 0);
  renderSpeechBubble(c, kFont, b, 0, 30);
  auto at = [&](int x, int y) { return pixels[y * 60 + x]; };
  // Body 24 × 12 centred on the tail (x 18–41, y 2–13); tail rows 13–18.
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(0, 0));
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(18, 2));   // rounded corner left as scene
  TEST_ASSERT_EQUAL_HEX16(bubble::kInk, at(18, 8));   // left outline
  TEST_ASSERT_EQUAL_HEX16(bubble::kFill, at(30, 4));  // inside
  TEST_ASSERT_EQUAL_HEX16(bubble::kInk, at(26, 7));   // the glyph
  TEST_ASSERT_EQUAL_HEX16(bubble::kFill, at(30, 13)); // tail opens the bottom outline
  TEST_ASSERT_EQUAL_HEX16(bubble::kInk, at(24, 13));  // outline beside the tail
  TEST_ASSERT_EQUAL_HEX16(bubble::kInk, at(30, 18));  // tip
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(30, 19));
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(42, 8));
  // A tail near the edge keeps the body inside the canvas.
  for (uint16_t& p : pixels) p = 0x1234;
  renderSpeechBubble(c, kFont, b, 0, 2);
  TEST_ASSERT_EQUAL_HEX16(bubble::kInk, at(0, 8));
  TEST_ASSERT_EQUAL_HEX16(0x1234, at(24, 8));
}

void setUp() {}
void tearDown() {}
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(one_chunk_is_ready_and_cleaned);
  RUN_TEST(chunks_join_even_inside_a_code_point);
  RUN_TEST(empty_or_blank_text_clears);
  RUN_TEST(bad_frames_are_rejected_and_drop_the_unfinished_text);
  RUN_TEST(text_longer_than_the_limit_is_rejected);
  RUN_TEST(wrap_breaks_at_spaces_and_cuts_long_words);
  RUN_TEST(pages_last_long_enough_then_the_bubble_hides);
  RUN_TEST(a_long_page_is_capped_at_eight_seconds);
  RUN_TEST(bubble_draws_body_tail_and_text_inside_the_canvas);
  return UNITY_END();
}
