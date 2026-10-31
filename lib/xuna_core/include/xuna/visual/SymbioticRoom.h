#pragma once
#include <cstdint>
#include <xuna/visual/VariantPlayer.h>

namespace xuna {

// SYM-3 S3.1: Epigenetic Phenotype Palette Tint.
// Derives a subtle, deterministic RGB565 tint from the pet's 16-byte PetId and
// dominant temperament trait (0..4) without duplicating sprite frame buffers.
enum class EpigeneticTint : uint8_t {
  Original = 0,
  CyanMint = 1,
  WarmCoral = 2,
  GoldenAmber = 3,
  CalmViolet = 4,
  EmeraldGlow = 5,
};

struct EpigeneticPalette {
  static EpigeneticTint tintForPet(const uint8_t* petId16, uint8_t dominantTrait) {
    if (!petId16) return EpigeneticTint::Original;
    uint8_t hash = dominantTrait;
    for (uint8_t i = 0; i < 16; ++i) {
      hash = uint8_t((hash * 31u) ^ petId16[i]);
    }
    const uint8_t idx = uint8_t((hash % 5u) + 1u);
    return static_cast<EpigeneticTint>(idx);
  }

  // Applies a subtle channel bias to mid/bright body pixels while preserving
  // dark outlines (r+g+b small) and near-white highlights.
  static uint16_t applyRgb565(uint16_t color, EpigeneticTint tint) {
    if (tint == EpigeneticTint::Original) return color;
    uint8_t r = uint8_t((color >> 11) & 0x1Fu);
    uint8_t g = uint8_t((color >> 5) & 0x3Fu);
    uint8_t b = uint8_t(color & 0x1Fu);
    // Preserve dark outlines and pure white eye highlights.
    if ((r < 4 && g < 8 && b < 4) || (r > 29 && g > 59 && b > 29)) {
      return color;
    }
    switch (tint) {
      case EpigeneticTint::CyanMint:
        g = g <= 59 ? uint8_t(g + 4) : 63;
        b = b <= 28 ? uint8_t(b + 3) : 31;
        break;
      case EpigeneticTint::WarmCoral:
        r = r <= 28 ? uint8_t(r + 3) : 31;
        if (b >= 2) b = uint8_t(b - 2);
        break;
      case EpigeneticTint::GoldenAmber:
        r = r <= 28 ? uint8_t(r + 3) : 31;
        g = g <= 60 ? uint8_t(g + 3) : 63;
        if (b >= 3) b = uint8_t(b - 3);
        break;
      case EpigeneticTint::CalmViolet:
        r = r <= 29 ? uint8_t(r + 2) : 31;
        b = b <= 28 ? uint8_t(b + 3) : 31;
        break;
      case EpigeneticTint::EmeraldGlow:
        g = g <= 58 ? uint8_t(g + 5) : 63;
        if (r >= 2) r = uint8_t(r - 2);
        break;
      case EpigeneticTint::Original:
        break;
    }
    return uint16_t((uint16_t(r) << 11) | (uint16_t(g) << 5) | uint16_t(b));
  }
};

// SYM-3 S3.1: Symbiotic Room 8x8 Micro-Props drawn inside the window glass.
enum class RoomPropKind : uint8_t {
  None = 0,
  FocusLamp = 1,       // Warm desk lamp / firefly when owner is in focus/deep work
  DreamBubble = 2,     // Sleeping dream bubble at night
  WatchdogBeacon = 3,  // Sentinel alert beacon when a watchdog triggers
  CoRegHeart = 4,      // Co-regulation empathy heart
};

struct SymbioticRoomProps {
  static constexpr uint8_t kPropSize = 8;

  static uint16_t colorFor(RoomPropKind kind) {
    switch (kind) {
      case RoomPropKind::FocusLamp: return 0xFFE0u;       // warm yellow-gold
      case RoomPropKind::DreamBubble: return 0xB7FFu;     // dreamy ice-cyan
      case RoomPropKind::WatchdogBeacon: return 0xFD20u;  // sentinel amber-orange
      case RoomPropKind::CoRegHeart: return 0xF99Eu;      // gentle rose-pink
      case RoomPropKind::None: break;
    }
    return 0xFFFFu;
  }

  static uint8_t bitmaskRow(RoomPropKind kind, uint8_t row) {
    if (row >= kPropSize) return 0;
    static const uint8_t kLamp[8] = {
        0x3C, 0x7E, 0xFF, 0x3C, 0x18, 0x18, 0x3C, 0x7E};
    static const uint8_t kDream[8] = {
        0x3C, 0x42, 0x99, 0xA1, 0x42, 0x3C, 0x10, 0x08};
    static const uint8_t kBeacon[8] = {
        0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0xFF, 0x00, 0x18};
    static const uint8_t kHeart[8] = {
        0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00};
    switch (kind) {
      case RoomPropKind::FocusLamp: return kLamp[row];
      case RoomPropKind::DreamBubble: return kDream[row];
      case RoomPropKind::WatchdogBeacon: return kBeacon[row];
      case RoomPropKind::CoRegHeart: return kHeart[row];
      case RoomPropKind::None: break;
    }
    return 0;
  }
};

// SYM-3 S3.2: 2-Button Rhythm Gesture Decoder & L-D3 Teachable Tricks.
// Detects 3-beat button rhythms (A-B-A, B-A-B, A-A-B) within 1.5 s without
// delaying single short presses.
enum class RhythmTrickId : uint8_t {
  None = 0,
  SpinDance = 1,    // A-B-A (GPIO0 -> GPIO14 -> GPIO0)
  CuriousPeek = 2,  // B-A-B (GPIO14 -> GPIO0 -> GPIO14)
  CozyNuzzle = 3,   // A-A-B (GPIO0 -> GPIO0 -> GPIO14)
};

class RhythmTrickDecoder {
 public:
  static constexpr uint32_t kMaxStepGapMs = 750;
  static constexpr uint32_t kMaxTotalSpanMs = 1500;

  void reset() {
    count_ = 0;
    bits_ = 0;
    firstAt_ = 0;
    lastAt_ = 0;
  }

  // Records a short press (`isA = true` for GPIO0 happy, `false` for GPIO14 idle).
  // Returns a non-None `RhythmTrickId` when a 3-beat rhythm completes in time.
  RhythmTrickId recordShortPress(bool isA, uint32_t now) {
    if (count_ > 0 && uint32_t(now - lastAt_) > kMaxStepGapMs) {
      count_ = 0;
      bits_ = 0;
    }
    if (count_ == 0) {
      firstAt_ = now;
    }
    lastAt_ = now;
    bits_ = uint8_t(((bits_ << 1) | (isA ? 1u : 0u)) & 0x07u);
    if (count_ < 3) ++count_;

    if (count_ == 3 && uint32_t(now - firstAt_) <= kMaxTotalSpanMs) {
      count_ = 0;
      const uint8_t pattern = bits_;
      bits_ = 0;
      if (pattern == 0x05u) return RhythmTrickId::SpinDance;    // 101: A-B-A
      if (pattern == 0x02u) return RhythmTrickId::CuriousPeek;  // 010: B-A-B
      if (pattern == 0x06u) return RhythmTrickId::CozyNuzzle;   // 110: A-A-B
    }
    return RhythmTrickId::None;
  }

  static VisualVariant variantFor(RhythmTrickId trick) {
    switch (trick) {
      case RhythmTrickId::SpinDance: return VisualVariant::BouncePlay;
      case RhythmTrickId::CuriousPeek: return VisualVariant::LookAround;
      case RhythmTrickId::CozyNuzzle: return VisualVariant::GreetNuzzle;
      case RhythmTrickId::None: break;
    }
    return VisualVariant::None;
  }

  static const char* speechFor(RhythmTrickId trick) {
    switch (trick) {
      case RhythmTrickId::SpinDance:
        return "Nhịp A-B-A! Mình xoay vòng cùng bạn nè!";
      case RhythmTrickId::CuriousPeek:
        return "Nhịp B-A-B! Ngó nghiêng canh bàn làm việc cùng bạn!";
      case RhythmTrickId::CozyNuzzle:
        return "Nhịp A-A-B! Dụi đầu nạp đầy năng lượng cho bạn nè!";
      case RhythmTrickId::None:
        break;
    }
    return "";
  }

 private:
  uint8_t count_ = 0;
  uint8_t bits_ = 0;
  uint32_t firstAt_ = 0;
  uint32_t lastAt_ = 0;
};

// Tracks L-D3 Teachable Trick repetitions & FSRS-inspired stability (Q4 days).
class TrickMasteryTracker {
 public:
  static constexpr uint8_t kTrickCount = 3;
  static constexpr uint8_t kMasteryReps = 3;

  void recordPractice(RhythmTrickId trick, uint32_t now) {
    const uint8_t idx = indexFor(trick);
    if (idx >= kTrickCount) return;
    if (reps_[idx] < 255) ++reps_[idx];
    // FSRS-lite stability growth: S' = S + 1.5 days (24 in Q4) per spaced practice.
    const uint16_t nextS = uint16_t(stabilityQ4_[idx] + 24u);
    stabilityQ4_[idx] = nextS > 1600u ? 1600u : nextS;
    lastAt_[idx] = now;
  }

  uint8_t reps(RhythmTrickId trick) const {
    const uint8_t idx = indexFor(trick);
    return idx < kTrickCount ? reps_[idx] : 0;
  }

  uint16_t stabilityDaysQ4(RhythmTrickId trick) const {
    const uint8_t idx = indexFor(trick);
    return idx < kTrickCount ? stabilityQ4_[idx] : 0;
  }

  bool mastered(RhythmTrickId trick) const {
    return reps(trick) >= kMasteryReps;
  }

  uint8_t masteredCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kTrickCount; ++i) {
      if (reps_[i] >= kMasteryReps) ++count;
    }
    return count;
  }

  uint16_t totalReps() const {
    return uint16_t(reps_[0]) + uint16_t(reps_[1]) + uint16_t(reps_[2]);
  }

 private:
  static uint8_t indexFor(RhythmTrickId trick) {
    const uint8_t raw = static_cast<uint8_t>(trick);
    return (raw >= 1 && raw <= kTrickCount) ? uint8_t(raw - 1) : 255;
  }

  uint8_t reps_[kTrickCount] = {0, 0, 0};
  uint16_t stabilityQ4_[kTrickCount] = {16, 16, 16};  // 1.0 day initial stability
  uint32_t lastAt_[kTrickCount] = {0, 0, 0};
};

}  // namespace xuna

