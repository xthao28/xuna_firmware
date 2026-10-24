#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <xuna/AnimationManager.h>
#include <xuna/learning/HabitPredictor.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/LifeCore.h>
#include <xuna/personality/Expression.h>
#include <xuna/visual/TextRaster.h>
namespace xuna {
// Device cards (owner-approved 2026-10-22): a status card on a GPIO0 hold and
// a pairing QR card on a hold of both buttons. Every user-visible string is a
// literal in THIS file: tools/generate_ui_font.py scans it for the glyph set.

struct CardFonts {
  const UiFont& title;
  const UiFont& value;
  const UiFont& label;
};

namespace card {
constexpr uint16_t kBackground = 0x18c4;  // #1A1820
constexpr uint16_t kDivider = 0x3988;     // #393342
constexpr uint16_t kLabel = 0x9c95;       // #9C93AE
constexpr uint16_t kValue = 0xf75b;       // #F3EAD8
constexpr uint16_t kTitle = 0xf628;       // #F6C445
constexpr uint16_t kBarFill = 0x94eb;     // #929D5A
constexpr uint16_t kQrDark = 0x0000;
constexpr uint16_t kQrLight = 0xffff;
}  // namespace card

inline const char* actionName(ActionId action) {
  switch (action) {
    case ActionId::Idle: return "thư giãn";
    case ActionId::Happy: return "vui vẻ";
    case ActionId::Sleep: return "ngủ";
    case ActionId::Curious: return "tò mò";
  }
  return "?";
}
inline const char* moodName(MoodWord mood) {
  switch (mood) {
    case MoodWord::Calm: return "bình yên";
    case MoodWord::Glad: return "vui";
    case MoodWord::Joyful: return "rất vui";
    case MoodWord::Low: return "hơi buồn";
    case MoodWord::Sad: return "buồn";
    case MoodWord::Bored: return "hơi chán";
  }
  return "?";
}
inline const char* stateName(PetState state) {
  switch (state) {
    case PetState::Idle: return "thư giãn";
    case PetState::Happy: return "vui vẻ";
    case PetState::Sleep: return "đang ngủ";
    case PetState::Curious: return "tò mò";
  }
  return "?";
}

// "45 phút", "3 giờ 5 phút", "4 ngày 2 giờ" (active lifetime, not wall time).
inline void formatLifetime(uint32_t seconds, char* out, size_t size) {
  const uint32_t minutes = seconds / 60, hours = minutes / 60;
  if (hours == 0) snprintf(out, size, "%u phút", unsigned(minutes));
  else if (hours < 48) snprintf(out, size, "%u giờ %u phút", unsigned(hours), unsigned(minutes % 60));
  else snprintf(out, size, "%u ngày %u giờ", unsigned(hours / 24), unsigned(hours % 24));
}

inline void hexPrefix(const uint8_t* bytes, size_t count, char* out) {
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < count; ++i) {
    out[2 * i] = kHex[bytes[i] >> 4];
    out[2 * i + 1] = kHex[bytes[i] & 15];
  }
  out[2 * count] = 0;
}

struct StatusCardModel {
  uint32_t activeSeconds = 0;
  uint8_t energyPct = 0;
  PetState state = PetState::Idle;
  ActionId quiet = ActionId::Idle, play = ActionId::Idle, greeting = ActionId::Idle;
  uint32_t feedbackCount = 0;
  bool arrivalKnown = false, weekend = false;
  uint8_t arrivalHour = 0, arrivalMinute = 0;
  char shortId[9] = {};  // first 4 PetId bytes as hex; empty before hatch
  MoodWord mood = MoodWord::Calm;  // personality PE-2 (Affect mood)
};

// The usual arrival is shown for today's day type when the time is trusted,
// otherwise for weekdays.
inline StatusCardModel makeStatusCard(const LifeCore& core, uint16_t energy, PetState state,
                                      const uint8_t* petId) {
  StatusCardModel m;
  const LifeSnapshotV2& s = core.snapshot();
  m.activeSeconds = s.counters.activeSeconds;
  m.energyPct = uint8_t(uint32_t(energy) * 100 / 65535);
  m.state = state;
  learnActionId(core.preferred(LearnInteraction::QuietRequest), m.quiet);
  learnActionId(core.preferred(LearnInteraction::PlayRequest), m.play);
  learnActionId(core.preferred(LearnInteraction::ButtonGreeting), m.greeting);
  m.feedbackCount = s.counters.feedbackCount;
  const uint64_t mono = core.mono64();
  if (core.clock().quality(mono) != ClockQuality::Unknown) {
    const int64_t local = int64_t(core.clock().utcMinute(mono)) + core.clock().tzOffsetMin();
    if (local > 0) m.weekend = HabitPredictor::weekend(uint32_t(local / 1440));
  }
  m.arrivalKnown = HabitPredictor::usualArrival(s.habit, m.weekend, m.arrivalHour, m.arrivalMinute);
  if (petId) hexPrefix(petId, 4, m.shortId);
  m.mood = Expression::moodWord(core.personality().affect().state());
  return m;
}

// 320 × 170: title row, then a 2 × 4 grid of label/value cells.
inline void renderStatusCard(Canvas& c, const CardFonts& f, const StatusCardModel& m) {
  fillRect(c, 0, 0, c.width, c.height, card::kBackground);
  const int titleEnd = drawText(c, f.title, 12, 24, "XUNA", card::kTitle);
  // Mood line next to the name (PE-2).
  const int moodX = drawText(c, f.label, titleEnd + 12, 22, "tâm trạng", card::kLabel);
  drawText(c, f.value, moodX + 6, 23, moodName(m.mood), card::kValue);
  if (m.shortId[0]) {
    char id[24];
    snprintf(id, sizeof(id), "mã %s", m.shortId);
    drawText(c, f.label, c.width - 12 - textWidth(f.label, id), 22, id, card::kLabel);
  }
  fillRect(c, 12, 32, c.width - 24, 1, card::kDivider);

  char lifetime[24], energy[8], learned[16], arrival[24];
  formatLifetime(m.activeSeconds, lifetime, sizeof(lifetime));
  snprintf(energy, sizeof(energy), "%u%%", unsigned(m.energyPct));
  snprintf(learned, sizeof(learned), "%u lần", unsigned(m.feedbackCount));
  if (m.arrivalKnown)
    snprintf(arrival, sizeof(arrival), "khoảng %02u:%02u", unsigned(m.arrivalHour),
             unsigned(m.arrivalMinute));
  else
    snprintf(arrival, sizeof(arrival), "chưa rõ");
  const char* labels[8] = {"Đã sống", "Năng lượng", "Bây giờ", "Đã học từ phản ứng",
                           "Lúc yên tĩnh thích", "Lúc chơi thích", "Được chào thì",
                           m.weekend ? "Cuối tuần chủ hay về" : "Chủ hay về"};
  const char* values[8] = {lifetime, energy, stateName(m.state), learned,
                           actionName(m.quiet), actionName(m.play), actionName(m.greeting),
                           arrival};
  for (uint8_t i = 0; i < 8; ++i) {
    const int x = i < 4 ? 12 : 166, y = 38 + (i % 4) * 33;
    drawText(c, f.label, x, y + 11, labels[i], card::kLabel);
    const int end = drawText(c, f.value, x, y + 28, values[i], card::kValue);
    if (i == 1) {  // energy bar after the percentage
      const int bx = end + 8, bw = 140 - (bx - x);
      if (bw > 10) {
        fillRect(c, bx, y + 20, bw, 6, card::kDivider);
        fillRect(c, bx, y + 20, bw * m.energyPct / 100, 6, card::kBarFill);
      }
    }
  }
}

// Pairing payload: PetId (32 hex) and the first 8 bytes of the device public
// key after the 0x04 prefix (16 hex), so the app can check it reached this
// device before the device-proof bind. Nothing secret.
constexpr size_t kPairingUriSize = 72;
inline bool pairingUri(const uint8_t* petId, const uint8_t* publicKey65, char* out, size_t size) {
  if (!petId || !publicKey65 || size < kPairingUriSize) return false;
  char pet[33], key[17];
  hexPrefix(petId, 16, pet);
  hexPrefix(publicKey65 + 1, 8, key);
  snprintf(out, size, "xuna://pair?pet=%s&key=%s", pet, key);
  return true;
}

// QR on the left (quiet zone included), instructions on the right.
// `dark(ctx, x, y)` reports one module of a size × size symbol.
typedef bool (*QrModule)(const void* ctx, int x, int y);
inline void renderPairingCard(Canvas& c, const CardFonts& f, const char* shortId, int size,
                              QrModule dark, const void* ctx) {
  fillRect(c, 0, 0, c.width, c.height, card::kBackground);
  const int quiet = 2, total = size + 2 * quiet;
  int scale = (c.height - 6) / (total > 0 ? total : 1);
  if (scale > 4) scale = 4;
  if (scale < 1) scale = 1;
  const int px = total * scale, ox = 8, oy = (c.height - px) / 2;
  fillRect(c, ox, oy, px, px, card::kQrLight);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      if (dark(ctx, x, y))
        fillRect(c, ox + (x + quiet) * scale, oy + (y + quiet) * scale, scale, scale, card::kQrDark);
  const int tx = ox + px + 14;
  drawText(c, f.title, tx, 34, "Ghép nối", card::kTitle);
  drawText(c, f.value, tx, 64, "Mở app XUNA,", card::kValue);
  drawText(c, f.value, tx, 84, "quét mã này", card::kValue);
  drawText(c, f.value, tx, 104, "để kết nối.", card::kValue);
  if (shortId && shortId[0]) {
    char id[24];
    snprintf(id, sizeof(id), "mã %s", shortId);
    drawText(c, f.label, tx, 132, id, card::kLabel);
  }
  drawText(c, f.label, tx, 156, "Bấm nút để đóng", card::kLabel);
}
}
