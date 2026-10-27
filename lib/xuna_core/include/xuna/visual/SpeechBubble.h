#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <xuna/visual/TextRaster.h>
namespace xuna {
// Speech subtitles (owner 2026-10-27): the app writes what XUNA says to
// characteristic 7a6e0010 (capability byte 3 bit 5) and the board shows it in
// a bubble above the pet, one page of up to three lines at a time. Display
// only: the text stays in RAM until replaced, is never logged, and never
// reaches the snapshot, life, learning or personality. Vietnamese and English.
//
// Frame: [0] version 1, [1] text id, [2] chunk index, [3] chunk count,
// [4..] UTF-8 bytes. The chunks of one text arrive in order (write with
// response) and may split a code point; index 0 starts a new text and drops
// any unfinished one. A single empty chunk clears the bubble.
class SpeechTextAssembler {
 public:
  static constexpr uint8_t kVersion = 1;
  static constexpr size_t kHeader = 4;
  static constexpr size_t kMaxBytes = 600;
  static constexpr uint8_t kMaxChunks = 64;
  static constexpr size_t kMaxFrame = 244;  // ATT MTU 247 - 3
  enum class Result : uint8_t { Partial, Ready, Cleared, Rejected };

  Result feed(const uint8_t* b, size_t n) {
    if (n < kHeader || n > kMaxFrame || b[0] != kVersion) return reject();
    const uint8_t id = b[1], index = b[2], count = b[3];
    const size_t payload = n - kHeader;
    if (!count || count > kMaxChunks || index >= count) return reject();
    if (index == 0) {
      open_ = true;
      id_ = id;
      count_ = count;
      next_ = 0;
      size_ = 0;
    } else if (!open_ || id != id_ || count != count_ || index != next_) {
      return reject();
    }
    if (size_ + payload > kMaxBytes) return reject();
    std::memcpy(raw_ + size_, b + kHeader, payload);
    size_ += payload;
    next_ = uint8_t(index + 1);
    if (next_ < count_) return Result::Partial;
    open_ = false;
    clean();
    return text_[0] ? Result::Ready : Result::Cleared;
  }
  // The last complete text: control bytes become spaces, runs of spaces
  // collapse, and the ends are trimmed. Bad UTF-8 is left to decodeUtf8.
  const char* text() const { return text_; }

 private:
  Result reject() {
    open_ = false;
    return Result::Rejected;
  }
  void clean() {
    size_t out = 0;
    bool space = true;  // drops leading spaces
    for (size_t i = 0; i < size_; ++i) {
      uint8_t c = uint8_t(raw_[i]);
      if (c < 0x20 || c == 0x7F) c = ' ';
      if (c == ' ') {
        if (space) continue;
        space = true;
      } else {
        space = false;
      }
      text_[out++] = char(c);
    }
    if (out && text_[out - 1] == ' ') --out;
    text_[out] = 0;
  }
  char raw_[kMaxBytes] = {};
  char text_[kMaxBytes + 1] = {};
  size_t size_ = 0;
  bool open_ = false;
  uint8_t id_ = 0, count_ = 0, next_ = 0;
};

// Byte range of one wrapped line inside the text.
struct TextLine {
  uint16_t begin, end;
};

// Greedy word wrap to `maxWidth` pixels; a word wider than a line is cut
// between code points. Trailing spaces are not part of a line. Returns the
// number of lines (at most `maxLines`; the rest of the text is dropped).
inline uint8_t wrapText(const UiFont& font, const char* text, uint16_t maxWidth,
                        TextLine* lines, uint8_t maxLines) {
  uint8_t count = 0;
  const char* p = text;
  while (count < maxLines) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* start = p;
    const char* space = nullptr;
    const char* end = nullptr;
    const char* next = nullptr;
    uint16_t width = 0;
    while (*p) {
      const char* at = p;
      const uint32_t cp = decodeUtf8(p);
      const uint16_t advance = advanceOf(font, cp);
      if (cp == ' ') space = at;
      if (width + advance > maxWidth && at > start) {
        if (space && space > start) {
          end = space;
          next = space + 1;
        } else {
          end = at;
          next = at;
        }
        break;
      }
      width = uint16_t(width + advance);
    }
    if (!end) end = next = p;
    while (end > start && end[-1] == ' ') --end;
    lines[count].begin = uint16_t(start - text);
    lines[count].end = uint16_t(end - text);
    ++count;
    p = next;
  }
  return count;
}

// What the bubble shows over time: pages of three lines, each long enough to
// read (about 60 ms per character, 2 to 8 s), the last one held 3 s longer,
// then hidden. A new text replaces the old one at once.
class SpeechBubble {
 public:
  static constexpr uint8_t kLinesPerPage = 3;
  static constexpr uint8_t kMaxLines = 30;
  static constexpr uint8_t kMaxPages = kMaxLines / kLinesPerPage;
  static constexpr uint32_t kHoldMs = 3000;

  void show(const char* text, const UiFont& font, uint16_t maxWidth, uint32_t now) {
    std::strncpy(text_, text, sizeof(text_) - 1);
    text_[sizeof(text_) - 1] = 0;
    lineCount_ = wrapText(font, text_, maxWidth, lines_, kMaxLines);
    shownAt_ = now;
    uint32_t total = 0;
    for (uint8_t page = 0; page < pageCount(); ++page) {
      total += pageMs(page);
      if (page + 1 == pageCount()) total += kHoldMs;
      ends_[page] = total;
    }
  }
  void clear() { lineCount_ = 0; }
  // Page on screen at `now`, or -1 when hidden.
  int page(uint32_t now) const {
    const uint32_t elapsed = now - shownAt_;
    for (uint8_t page = 0; page < pageCount(); ++page)
      if (elapsed < ends_[page]) return page;
    return -1;
  }
  uint8_t pageCount() const {
    return uint8_t((lineCount_ + kLinesPerPage - 1) / kLinesPerPage);
  }
  // Lines of `page`: first index and how many.
  uint8_t firstLine(int page) const { return uint8_t(page * kLinesPerPage); }
  uint8_t linesOn(int page) const {
    const int left = int(lineCount_) - page * kLinesPerPage;
    return uint8_t(left < 0 ? 0 : left > kLinesPerPage ? kLinesPerPage : left);
  }
  const TextLine& line(uint8_t index) const { return lines_[index]; }
  const char* text() const { return text_; }

 private:
  uint32_t pageMs(uint8_t page) const {
    uint32_t chars = 0;
    for (uint8_t i = firstLine(page); i < firstLine(page) + linesOn(page); ++i)
      for (const char* p = text_ + lines_[i].begin; p < text_ + lines_[i].end;) {
        decodeUtf8(p);
        ++chars;
      }
    const uint32_t ms = 800 + 60 * chars;
    return ms < 2000 ? 2000 : ms > 8000 ? 8000 : ms;
  }
  char text_[SpeechTextAssembler::kMaxBytes + 1] = {};
  TextLine lines_[kMaxLines] = {};
  uint32_t ends_[kMaxPages] = {};
  uint32_t shownAt_ = 0;
  uint8_t lineCount_ = 0;
};

namespace bubble {
constexpr uint16_t kFill = 0xf75b;    // #F3EAD8, the card value colour
constexpr uint16_t kInk = 0x18c4;     // #1A1820, the card background
constexpr uint8_t kPadX = 8, kPadTop = 5, kPadBottom = 4, kTail = 6, kRadius = 5;
}  // namespace bubble

// Draws `page` into `c`, which already holds the scene behind the bubble.
// The body fits its widest line, grows upward from a tail at `tailX` that
// points down at XUNA, and stays inside the canvas.
inline void renderSpeechBubble(Canvas& c, const UiFont& font, const SpeechBubble& b, int page,
                               int tailX) {
  const uint8_t first = b.firstLine(page), count = b.linesOn(page);
  if (!count) return;
  char buffer[SpeechTextAssembler::kMaxBytes + 1];
  uint16_t widest = 0;
  for (uint8_t i = first; i < first + count; ++i) {
    const TextLine& l = b.line(i);
    std::memcpy(buffer, b.text() + l.begin, l.end - l.begin);
    buffer[l.end - l.begin] = 0;
    const uint16_t w = textWidth(font, buffer);
    if (w > widest) widest = w;
  }
  int bodyW = widest + 2 * bubble::kPadX;
  if (bodyW < 4 * bubble::kTail) bodyW = 4 * bubble::kTail;
  if (bodyW > c.width) bodyW = c.width;
  const int bodyH = bubble::kPadTop + count * font.lineHeight + bubble::kPadBottom;
  const int bodyBottom = c.height - bubble::kTail;  // first row below the body
  const int bodyY = bodyBottom - bodyH < 0 ? 0 : bodyBottom - bodyH;
  int bodyX = tailX - bodyW / 2;
  if (bodyX < 0) bodyX = 0;
  if (bodyX + bodyW > c.width) bodyX = c.width - bodyW;
  // Rounded body with a one-pixel outline.
  const int r = bubble::kRadius;
  for (int y = bodyY; y < bodyBottom; ++y)
    for (int x = bodyX; x < bodyX + bodyW; ++x) {
      const int dx = x < bodyX + r ? bodyX + r - x : x >= bodyX + bodyW - r ? x - (bodyX + bodyW - r - 1) : 0;
      const int dy = y < bodyY + r ? bodyY + r - y : y >= bodyBottom - r ? y - (bodyBottom - r - 1) : 0;
      const int d2 = dx * dx + dy * dy;
      if (d2 > r * r) continue;
      const bool edge = x == bodyX || x == bodyX + bodyW - 1 || y == bodyY ||
                        y == bodyBottom - 1 || d2 > (r - 1) * (r - 1);
      c.pixels[y * c.width + x] = edge ? bubble::kInk : bubble::kFill;
    }
  // Tail: a triangle from the body's bottom row (opening the outline there)
  // down to a point, outlined on its slanted sides.
  for (int row = 0; row < bubble::kTail; ++row) {
    const int half = bubble::kTail - 1 - row, y = bodyBottom - 1 + row;
    if (y >= c.height) break;
    for (int x = tailX - half; x <= tailX + half; ++x) {
      if (x < 0 || x >= c.width) continue;
      const bool edge = x == tailX - half || x == tailX + half;
      c.pixels[y * c.width + x] = edge ? bubble::kInk : bubble::kFill;
    }
  }
  for (uint8_t i = 0; i < count; ++i) {
    const TextLine& l = b.line(uint8_t(first + i));
    std::memcpy(buffer, b.text() + l.begin, l.end - l.begin);
    buffer[l.end - l.begin] = 0;
    drawText(c, font, bodyX + bubble::kPadX, bodyY + bubble::kPadTop + font.ascent + i * font.lineHeight,
             buffer, bubble::kInk);
  }
}
}  // namespace xuna
