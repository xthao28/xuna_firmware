#pragma once

#include <cstdint>
#include <xuna/base/Fixed.h>

namespace xuna {

// M04.5 placeholder parameters. They are deliberately explicit and remain
// Proposed until Life Lab tuning; no wall-clock or offline duration is used.
struct LifeTuning {
  uint16_t initialEnergy = 65535;
  uint16_t initialStimulation = 0;
  uint16_t energyDecayPerMinute = 1024;
  uint16_t stimulationDecayPerMinute = 2048;
  // Applied instead of energy decay while the caller reports recovery
  // (Resting/Sleeping). Placeholder: about four minutes of Resting.
  uint16_t energyRecoveryPerMinute = 2048;
  uint32_t decayPeriodMs = 60000;
};

struct NeedsState {
  // Both fields are bounded Q0.16 raw values: 0..65535.
  uint16_t energy = 65535;
  uint16_t stimulation = 0;
};

class Needs {
 public:
  explicit Needs(const LifeTuning& tuning = LifeTuning()) : tuning_(tuning) {
    reset(0);
  }

  void reset(uint32_t now) {
    state_.energy = tuning_.initialEnergy;
    state_.stimulation = tuning_.initialStimulation;
    lastMonoMs_ = now;
    energyRemainder_ = 0;
    recoveryRemainder_ = 0;
    stimulationRemainder_ = 0;
  }

  // Applies a closed-form linear change. Remainders make split updates exactly
  // equivalent to one catch-up update, including across one uint32_t wrap.
  // `recovering` describes the whole elapsed interval: callers pass the state
  // that held since the previous update, so energy rises instead of decaying.
  void update(uint32_t now, bool recovering = false) {
    const uint32_t elapsed = now - lastMonoMs_;
    lastMonoMs_ = now;
    if (!elapsed || !tuning_.decayPeriodMs) return;
    if (recovering)
      recover(state_.energy, tuning_.energyRecoveryPerMinute, elapsed,
              recoveryRemainder_);
    else
      decay(state_.energy, tuning_.energyDecayPerMinute, elapsed,
            energyRemainder_);
    decay(state_.stimulation, tuning_.stimulationDecayPerMinute, elapsed,
          stimulationRemainder_);
  }

  const NeedsState& state() const { return state_; }
  Q0_16 energy() const { return Q0_16::fromRaw(state_.energy); }
  Q0_16 stimulation() const { return Q0_16::fromRaw(state_.stimulation); }
  uint32_t lastMonoMs() const { return lastMonoMs_; }

 private:
  void decay(uint16_t& value, uint16_t rate, uint32_t elapsed,
             uint32_t& remainder) const {
    const uint64_t work = uint64_t(rate) * elapsed + remainder;
    const uint64_t amount = work / tuning_.decayPeriodMs;
    remainder = static_cast<uint32_t>(work % tuning_.decayPeriodMs);
    value = amount >= value ? 0 : static_cast<uint16_t>(value - amount);
  }

  void recover(uint16_t& value, uint16_t rate, uint32_t elapsed,
               uint32_t& remainder) const {
    const uint64_t work = uint64_t(rate) * elapsed + remainder;
    const uint64_t amount = work / tuning_.decayPeriodMs;
    remainder = static_cast<uint32_t>(work % tuning_.decayPeriodMs);
    const uint32_t room = 65535U - value;
    value = amount >= room ? 65535 : static_cast<uint16_t>(value + amount);
  }

  LifeTuning tuning_;
  NeedsState state_;
  uint32_t lastMonoMs_ = 0;
  uint32_t energyRemainder_ = 0;
  uint32_t recoveryRemainder_ = 0;
  uint32_t stimulationRemainder_ = 0;
};

enum class MoodLabel : uint8_t { Calm, Excited, Sleepy, Curious, Overwhelmed };

struct MoodSignals {
  int16_t valenceDelta = 0;  // signed Q1.15 adjustment
  int16_t arousalDelta = 0;  // signed Q0.16 adjustment
  bool interaction = false;
  bool positiveFeedback = false;
  bool negativeFeedback = false;
};

struct MoodState {
  uint16_t arousal = 0;  // Q0.16, 0..65535
  int16_t valence = 0;   // Q1.15, -32768..32767
  MoodLabel label = MoodLabel::Calm;
};

// Mood is derived and transient. It is never persisted by this model.
class Mood {
 public:
  void reset() {
    state_ = MoodState();
    state_.valence = 0;
  }

  void update(const NeedsState& needs, const MoodSignals& signals = MoodSignals()) {
    int32_t arousal = needs.stimulation;
    if (signals.interaction) arousal += 8192;
    if (signals.positiveFeedback) arousal += 2048;
    if (signals.negativeFeedback) arousal -= 4096;
    arousal += signals.arousalDelta;
    state_.arousal = clampUnsigned(arousal);

    int32_t valence = static_cast<int32_t>(needs.energy) - 32768;
    valence += signals.valenceDelta;
    if (signals.positiveFeedback) valence += 4096;
    if (signals.negativeFeedback) valence -= 4096;
    state_.valence = clampSigned(valence);
    latchLabel(needs);
  }

  const MoodState& state() const { return state_; }
  MoodLabel label() const { return state_.label; }

 private:
  static uint16_t clampUnsigned(int32_t value) {
    if (value <= 0) return 0;
    if (value >= 65535) return 65535;
    return static_cast<uint16_t>(value);
  }

  static int16_t clampSigned(int32_t value) {
    if (value <= -32768) return -32768;
    if (value >= 32767) return 32767;
    return static_cast<int16_t>(value);
  }

  void latchLabel(const NeedsState& needs) {
    // Exit hysteresis is checked before a new label can be entered. This
    // prevents rapid threshold flicker when a value hovers near a boundary.
    switch (state_.label) {
      case MoodLabel::Sleepy:
        if (needs.energy <= 24576) return;
        state_.label = MoodLabel::Calm;
        return;
      case MoodLabel::Overwhelmed:
        if (needs.stimulation >= 49152) return;
        state_.label = MoodLabel::Calm;
        return;
      case MoodLabel::Excited:
        if (state_.arousal >= 36864) return;
        state_.label = MoodLabel::Calm;
        return;
      case MoodLabel::Curious:
        if (needs.stimulation >= 16384) return;
        state_.label = MoodLabel::Calm;
        return;
      case MoodLabel::Calm:
        break;
    }
    if (needs.energy <= 16384 && state_.arousal < 32768) {
      state_.label = MoodLabel::Sleepy;
    } else if (needs.stimulation >= 57344) {
      state_.label = MoodLabel::Overwhelmed;
    } else if (state_.arousal >= 45056 && state_.valence >= 0) {
      state_.label = MoodLabel::Excited;
    } else if (needs.stimulation >= 24576 && needs.energy >= 32768 &&
               state_.valence >= 0) {
      state_.label = MoodLabel::Curious;
    }
  }

  MoodState state_;
};

}  // namespace xuna