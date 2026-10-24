#pragma once

#include <cstdint>

#include <xuna/CoreConfig.h>
#include <xuna/personality/Personality.h>

namespace xuna {

// Personality PE-2 (G1-c, owner 2026-10-24): the first safe expression of
// temperament and mood. It changes only how XUNA moves (animation frame
// period and how long a reaction stays on screen), never what it chooses,
// and works with the existing art. Every constant is [P].
//
// The mapping uses the trait range XUNA really has (±0.3 reaches the full
// effect), not ±1, so an innate ±0.15 is already half the effect:
// - liveliness = tempo + mood arousal / 2 → frame period 100 ms ∓ 15 %;
// - feeling = sensitivity + |emotion valence| / 2 → reaction 2400 ms ± 15 %.
// A neutral genome with a calm mood gives exactly today's 100 ms / 2400 ms.
struct ExpressionStyle {
  uint16_t framePeriodMs = 100;
  uint16_t reactionMs = static_cast<uint16_t>(kHappyDurationMs);
};

// Coarse word for the mood (status card). Order is append only.
enum class MoodWord : uint8_t { Calm = 0, Glad, Joyful, Low, Sad, Bored };

class Expression {
 public:
  static constexpr uint16_t kFramePeriodMs = 100;   // PetVisualAnimation default
  static constexpr uint16_t kFrameSwingMs = 15;     // ±15 %
  static constexpr uint16_t kReactionSwingMs = 360; // ±15 % of 2400 ms
  static constexpr int16_t kFullEffect = 4915;      // 0.3 in Q1.14
  static constexpr int16_t kGlad = 983;             // mood valence 0.06
  static constexpr int16_t kJoyful = 4096;          // 0.25
  static constexpr int16_t kBored = -1638;          // mood arousal −0.1

  static ExpressionStyle styleFor(const Personality& p) {
    return styleFor(p.genome(), p.affect().state());
  }

  static ExpressionStyle styleFor(const TemperamentState& genome, const AffectState& affect) {
    ExpressionStyle s;
    const int32_t lively = limit(int32_t(Temperament::effective(genome, TraitId::Tempo)) +
                                 affect.moodArousal / 2);
    const int32_t emotion = affect.emotionValence < 0 ? -int32_t(affect.emotionValence)
                                                      : int32_t(affect.emotionValence);
    const int32_t feeling = limit(int32_t(Temperament::effective(genome, TraitId::Sensitivity)) +
                                  emotion / 2);
    s.framePeriodMs = static_cast<uint16_t>(int32_t(kFramePeriodMs) -
                                            int32_t(kFrameSwingMs) * lively / kFullEffect);
    s.reactionMs = static_cast<uint16_t>(int32_t(kHappyDurationMs) +
                                         int32_t(kReactionSwingMs) * feeling / kFullEffect);
    return s;
  }

  static MoodWord moodWord(const AffectState& a) {
    if (a.moodValence >= kJoyful) return MoodWord::Joyful;
    if (a.moodValence >= kGlad) return MoodWord::Glad;
    if (a.moodValence <= -kJoyful) return MoodWord::Sad;
    if (a.moodValence <= -kGlad) return MoodWord::Low;
    if (a.moodArousal <= kBored) return MoodWord::Bored;
    return MoodWord::Calm;
  }

 private:
  static int32_t limit(int32_t v) {
    return v > kFullEffect ? kFullEffect : v < -kFullEffect ? -kFullEffect : v;
  }
};

}  // namespace xuna
