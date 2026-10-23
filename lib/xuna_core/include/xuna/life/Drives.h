#pragma once

#include <cstdint>

#include <xuna/life/ActionRegistry.h>
#include <xuna/personality/Temperament.h>

namespace xuna {

// Personality PE-1b (plan docs/roadmap/personality, 2026-10-23): drives that
// make XUNA *want* things, with speeds set by the temperament genome.
// Lab only until gate G1; Needs (energy, stimulation) stays unchanged and
// nothing on the board reads drives yet. Every rate and threshold is [P].
//
// All drives are Q0.16 (0..65535), transient (never persisted) and advance in
// whole minutes, so split updates equal one catch-up update. Rules:
// - explore rises unless resting; a curious action satisfies it.
// - play rises only with energy ≥ half (else it fades); happy satisfies it.
// - social rises for kLongingMinutes after the last owner contact up to a
//   ceiling, then settles back to 0: a long absence never turns into sadness
//   (AGENTS §6: do not penalise downtime). Owner contact satisfies it.
// - boredom rises unless resting; owner contact or a different action is new.
struct DriveState {
  uint16_t explore = 0;
  uint16_t social = 0;
  uint16_t play = 0;
  uint16_t boredom = 0;
};

// Per-minute rates in Q0.16 raw units, derived from the genome.
struct DriveRates {
  uint16_t explore = 512;      // ≈ 2 h from 0 to full at a neutral genome
  uint16_t play = 384;         // ≈ 3 h
  uint16_t socialRise = 512;
  uint16_t socialCeiling = 45875;  // 0.7
  uint16_t boredom = 256;      // ≈ 4 h
};

class Drives {
 public:
  static constexpr uint32_t kMinuteMs = 60000;
  static constexpr uint16_t kLongingMinutes = 120;  // social rises this long
  static constexpr uint16_t kSocialSettle = 512;    // per minute afterwards
  static constexpr uint16_t kPlayFade = 256;        // per minute when tired
  static constexpr uint16_t kPlayEnergy = 32768;    // play needs energy ≥ half
  static constexpr uint16_t kSatisfy = 32768;       // one satisfying act halves…
  static constexpr uint16_t kContactSatisfy = 49152;
  static constexpr uint16_t kNovelty = 32768;
  static constexpr uint16_t kSmallNovelty = 8192;   // XUNA did something else
  // Longer gaps are applied as this many minutes: every drive has reached
  // its resting value long before (tested), so the result is the same.
  static constexpr uint32_t kMaxCatchUpMinutes = 1440;

  // Speeds scale by (1 + effective trait): ±0.15 at birth, up to ±0.75 later.
  static DriveRates ratesFor(const TemperamentState& genome) {
    DriveRates r;
    r.explore = scale(r.explore, Temperament::effective(genome, TraitId::Curiosity));
    r.play = scale(r.play, Temperament::effective(genome, TraitId::Playfulness));
    const int16_t social = Temperament::effective(genome, TraitId::Sociability);
    r.socialRise = scale(r.socialRise, social);
    r.socialCeiling = scale(r.socialCeiling, social);
    r.boredom = scale(r.boredom, Temperament::effective(genome, TraitId::Boredom));
    return r;
  }

  explicit Drives(const DriveRates& rates = DriveRates()) : rates_(rates) {}

  void setRates(const DriveRates& rates) { rates_ = rates; }
  void reset(uint32_t now) {
    state_ = DriveState();
    lastMs_ = now;
    remainderMs_ = 0;
    sinceContact_ = 0xffff;  // no owner seen yet: nothing to long for
    lastAction_ = 0xff;
  }

  // Advance to `now`. `energy` and `resting` describe the whole interval
  // since the previous update, as with Needs::update.
  void update(uint32_t now, uint16_t energy, bool resting) {
    const uint64_t total = uint64_t(uint32_t(now - lastMs_)) + remainderMs_;
    lastMs_ = now;
    const uint64_t minutes = total / kMinuteMs;
    remainderMs_ = static_cast<uint32_t>(total % kMinuteMs);
    const uint32_t ticks = minutes > kMaxCatchUpMinutes ? kMaxCatchUpMinutes
                                                         : static_cast<uint32_t>(minutes);
    for (uint32_t i = 0; i < ticks; ++i) tick(energy, resting);
    const uint64_t skipped = minutes - ticks;
    sinceContact_ = sinceContact_ + skipped >= 0xffff
                        ? 0xffff : static_cast<uint16_t>(sinceContact_ + skipped);
  }

  // Any owner input (press, app command, chat turn).
  void ownerContact() {
    state_.social = lower(state_.social, kContactSatisfy);
    state_.boredom = lower(state_.boredom, kNovelty);
    sinceContact_ = 0;
  }

  // XUNA showed `action` (whoever chose it).
  void acted(ActionId action) {
    const uint8_t id = static_cast<uint8_t>(action);
    if (action == ActionId::Curious) state_.explore = lower(state_.explore, kSatisfy);
    if (action == ActionId::Happy) state_.play = lower(state_.play, kSatisfy);
    if (id != lastAction_) state_.boredom = lower(state_.boredom, kSmallNovelty);
    lastAction_ = id;
  }

  const DriveState& state() const { return state_; }
  const DriveRates& rates() const { return rates_; }
  uint16_t minutesSinceContact() const { return sinceContact_; }

 private:
  void tick(uint16_t energy, bool resting) {
    if (!resting) {
      state_.explore = raise(state_.explore, rates_.explore, 65535);
      state_.boredom = raise(state_.boredom, rates_.boredom, 65535);
      state_.play = energy >= kPlayEnergy ? raise(state_.play, rates_.play, 65535)
                                          : lower(state_.play, kPlayFade);
    }
    if (sinceContact_ < kLongingMinutes)
      state_.social = raise(state_.social, rates_.socialRise, rates_.socialCeiling);
    else
      state_.social = lower(state_.social, kSocialSettle);
    if (sinceContact_ < 0xffff) ++sinceContact_;
  }

  static uint16_t scale(uint16_t base, int16_t trait) {
    const int32_t value = (int32_t(base) * (kTraitOne + trait)) / kTraitOne;
    return value <= 0 ? 0 : value >= 65535 ? 65535 : static_cast<uint16_t>(value);
  }
  static uint16_t raise(uint16_t value, uint16_t amount, uint16_t ceiling) {
    if (value >= ceiling) return value;  // a ceiling never pulls a drive down
    const uint32_t next = uint32_t(value) + amount;
    return next >= ceiling ? ceiling : static_cast<uint16_t>(next);
  }
  static uint16_t lower(uint16_t value, uint16_t amount) {
    return amount >= value ? 0 : static_cast<uint16_t>(value - amount);
  }

  DriveRates rates_;
  DriveState state_;
  uint32_t lastMs_ = 0;
  uint32_t remainderMs_ = 0;
  uint16_t sinceContact_ = 0xffff;
  uint8_t lastAction_ = 0xff;
};

}  // namespace xuna
