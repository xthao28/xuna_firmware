#pragma once

#include <cstdint>

#include <xuna/personality/Temperament.h>

namespace xuna {

// Personality PE-1c (plan docs/roadmap/personality, 2026-10-23): emotion and
// mood as two coupled springs (WASABI idea), emotions appraised from what
// XUNA expected (GAMYGDALA idea) and competence = how well it predicts
// (MicroPsi idea). Lab only until gate G1; the existing Mood in Needs.h
// still drives the board. Every constant is [P].
//
// O1 (delegated default 2026-10-23, revisable): sensitivity only makes
// emotions stronger and slower to fade. Mood has a floor and always relaxes
// back to the pet's baseline; nothing about time or absence makes it sad.
//
// Units: Q1.14 (16384 = 1.0). Emotion is an impulse that fades each minute;
// mood follows emotion slowly (inertia from the genome) and is pulled back
// to its prevalence (MoodBaseline × 0.25). Minute ticks make split updates
// equal one catch-up update.
enum class Appraisal : uint8_t {
  None = 0,
  Joy,             // petted when it did not expect it
  Satisfaction,    // petted as expected
  Disappointment,  // shooed although it expected a pet
  Distress,        // shooed, roughly as expected (mild)
  Welcome,         // owner arrived (anticipated or not)
};

struct AffectState {
  int16_t emotionValence = 0;
  int16_t emotionArousal = 0;
  int16_t moodValence = 0;
  int16_t moodArousal = 0;
  uint16_t competenceQ16 = 32768;  // EMA of 1 − |outcome − p̂|; 0.5 at birth
  Appraisal last = Appraisal::None;
};

struct AffectParameters {
  int16_t gain = 8192;           // emotion size of a full surprise (0.5)
  int16_t retain = 12288;        // emotion kept per minute (0.75)
  int16_t massQ14 = 16384;       // mood inertia (1.0)
  int16_t prevalence = 0;        // mood valence it settles at
};

class Affect {
 public:
  static constexpr uint32_t kMinuteMs = 60000;
  static constexpr int16_t kMoodFloor = -8192;  // mood valence never below −0.5
  static constexpr int16_t kSlope = 2048;       // emotion → mood per minute (1/8)
  static constexpr int16_t kRelax = 512;        // pull back to prevalence (1/32)
  static constexpr uint32_t kMaxCatchUpMinutes = 1440;
  static constexpr uint32_t kCompetenceShift = 4;  // EMA 1/16

  static AffectParameters parametersFor(const TemperamentState& genome) {
    AffectParameters p;
    const int32_t sensitivity = Temperament::effective(genome, TraitId::Sensitivity);
    // Sensitive: up to 1.75× the impulse and slower fading (0.75 → ≤ 0.9).
    p.gain = clamp(int32_t(p.gain) * (kTraitOne + sensitivity) / kTraitOne, 0, kTraitOne);
    p.retain = clamp(int32_t(p.retain) + sensitivity / 4, 4096, 14745);
    // Inertia: mass 0.5…1.5.
    p.massQ14 = clamp(kTraitOne + Temperament::effective(genome, TraitId::MoodInertia) / 2,
                      8192, 24576);
    p.prevalence = static_cast<int16_t>(
        Temperament::effective(genome, TraitId::MoodBaseline) / 4);
    return p;
  }

  explicit Affect(const AffectParameters& parameters = AffectParameters())
      : p_(parameters) {
    reset(0);
  }

  void setParameters(const AffectParameters& parameters) { p_ = parameters; }
  void reset(uint32_t now) {
    state_ = AffectState();
    state_.moodValence = p_.prevalence;
    lastMs_ = now;
    remainderMs_ = 0;
  }

  // The owner reacted to a choice XUNA made while expecting approval with
  // probability `predictedQ16` (V3Decision::predictedQ16).
  Appraisal feedback(bool approved, uint16_t predictedQ16) {
    const int32_t p = predictedQ16 >> 2;  // Q0.16 → Q1.14
    const int32_t surprise = approved ? kTraitOne - p : p;
    // Half the impulse always, the other half scales with surprise.
    const int32_t size = int32_t(p_.gain) * (kTraitOne + surprise) / (2 * kTraitOne);
    const Appraisal kind = approved ? (p < kTraitOne / 2 ? Appraisal::Joy : Appraisal::Satisfaction)
                                    : (p >= kTraitOne / 2 ? Appraisal::Disappointment
                                                          : Appraisal::Distress);
    impulse(approved ? size : -size, int32_t(p_.gain) * surprise / kTraitOne);
    // Competence: EMA of 1 − |outcome − p̂| in Q0.16 (surprise is Q1.14).
    const int32_t miss = surprise * 4 > 65535 ? 65535 : surprise * 4;
    const int32_t c = state_.competenceQ16;
    state_.competenceQ16 = static_cast<uint16_t>(c + (65535 - miss - c) / (1 << kCompetenceShift));
    state_.last = kind;
    return kind;
  }

  // The owner is here again. Never a negative appraisal: XUNA does not
  // blame the owner for being away. An anticipated arrival is calmer.
  Appraisal ownerArrived(bool anticipated) {
    impulse(p_.gain / 2, anticipated ? p_.gain / 4 : p_.gain / 2);
    state_.last = Appraisal::Welcome;
    return Appraisal::Welcome;
  }

  // PE-4c: a conversation tag felt right away. Shares of the full impulse
  // (Q1.14, ±1); the mood floor still holds. Not an appraisal of a choice,
  // so competence and `last` stay as they were.
  void conversation(int16_t valenceShare, int16_t arousalShare) {
    impulse(int32_t(p_.gain) * valenceShare / kTraitOne, int32_t(p_.gain) * arousalShare / kTraitOne);
  }

  // Advance to `now`. `boredomQ16` (Drives) lowers where arousal settles.
  void update(uint32_t now, uint16_t boredomQ16) {
    const uint64_t total = uint64_t(uint32_t(now - lastMs_)) + remainderMs_;
    lastMs_ = now;
    const uint64_t minutes = total / kMinuteMs;
    remainderMs_ = static_cast<uint32_t>(total % kMinuteMs);
    const uint32_t ticks = minutes > kMaxCatchUpMinutes ? kMaxCatchUpMinutes
                                                         : static_cast<uint32_t>(minutes);
    const int16_t arousalRest = static_cast<int16_t>(-int32_t(boredomQ16 >> 2) / 4);
    for (uint32_t i = 0; i < ticks; ++i) tick(arousalRest);
  }

  const AffectState& state() const { return state_; }
  const AffectParameters& parameters() const { return p_; }

 private:
  void impulse(int32_t valence, int32_t arousal) {
    state_.emotionValence = clamp(int32_t(state_.emotionValence) + valence, -kTraitOne, kTraitOne);
    state_.emotionArousal = clamp(int32_t(state_.emotionArousal) + arousal, -kTraitOne, kTraitOne);
  }

  void tick(int16_t arousalRest) {
    state_.moodValence = spring(state_.moodValence, state_.emotionValence, p_.prevalence);
    if (state_.moodValence < kMoodFloor) state_.moodValence = kMoodFloor;
    state_.moodArousal = spring(state_.moodArousal, state_.emotionArousal, arousalRest);
    // Division truncates toward zero, so emotions always fade to exactly 0.
    state_.emotionValence =
        static_cast<int16_t>(int32_t(state_.emotionValence) * p_.retain / kTraitOne);
    state_.emotionArousal =
        static_cast<int16_t>(int32_t(state_.emotionArousal) * p_.retain / kTraitOne);
  }

  // mood += (slope·emotion − relax·(mood − rest)) / mass
  int16_t spring(int16_t mood, int16_t emotion, int16_t rest) const {
    const int32_t push = int32_t(emotion) * kSlope / kTraitOne;
    const int32_t pull = (int32_t(mood) - rest) * kRelax / kTraitOne;
    const int32_t step = (push - pull) * kTraitOne / p_.massQ14;
    return clamp(int32_t(mood) + step, -kTraitOne, kTraitOne);
  }

  static int16_t clamp(int32_t value, int32_t low, int32_t high) {
    return static_cast<int16_t>(value < low ? low : value > high ? high : value);
  }

  AffectParameters p_;
  AffectState state_;
  uint32_t lastMs_ = 0;
  uint32_t remainderMs_ = 0;
};

}  // namespace xuna
