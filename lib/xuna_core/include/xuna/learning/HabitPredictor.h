#pragma once

#include <cstdint>

namespace xuna {

// Routine learning (plan §9.7, owner-approved 2026-10-19 as part C of the
// reaction-learning change): XUNA learns WHEN the owner usually comes to it
// and anticipates that time. Pure integer code, no clock or I/O: the core
// passes trusted local time in.
//
// - 48 slots = 24 local hours × {weekday, weekend}; counts in Q8.8.
// - One observation per interaction session (a new session starts after
//   `kSessionGapMs` without owner input), only with trusted time.
// - On a new local day d, every count is multiplied by λ^(d − lastDay).
// - P(slot) = (h + κ) / Σ(h + κ).
// - Prequential: before each update the observed slot is scored (Brier)
//   against the uniform baseline; the gain is an EMA. Behaviour is gated on
//   enough evidence AND a positive gain, so a random owner never "teaches"
//   a routine (plan §9.9 discipline).
// Values marked [P] are provisional until Life Lab / board data.
struct HabitState {
  static constexpr uint8_t kSlots = 48;
  uint16_t counts[kSlots] = {};  // Q8.8
  uint8_t minutes[kSlots] = {};  // mean arrival minute within the hour (EMA)
  uint16_t lastDay = 0;          // local days since 2020-01-01; 0 = never
  int32_t gainQ16 = 0;           // EMA of Brier(uniform) − Brier(model)
  uint16_t sessions = 0;         // observed sessions (saturating)

  bool empty() const { return sessions == 0; }
};

class HabitPredictor {
 public:
  static constexpr uint32_t kSessionGapMs = 30UL * 60UL * 1000UL;  // [P]
  static constexpr uint32_t kLambdaQ16 = 62259;   // 0.95 per day [P]
  static constexpr uint32_t kKappaQ8 = 128;       // 0.5 [P]
  static constexpr uint32_t kMinEvidenceQ8 = 10U << 8;  // nEff ≥ 10 [P]
  static constexpr int32_t kGainMarginQ16 = 1311;  // 0.02 Brier [P]
  static constexpr uint8_t kGainShift = 3;         // EMA weight 1/8 [P]
  // A slot is "expected" when its probability is ≥ 4× uniform [P].
  static constexpr uint32_t kExpectRatio = 4;
  static constexpr uint32_t kLeadMinutes = 20;     // anticipate this early [P]

  // 2020-01-01 (local day 0) was a Wednesday; Monday = 0.
  static bool weekend(uint32_t localDay) { return (localDay + 2) % 7 >= 5; }
  static uint8_t slot(uint32_t localDay, uint32_t minuteOfDay) {
    const uint8_t hour = uint8_t((minuteOfDay % 1440) / 60);
    return uint8_t(hour + (weekend(localDay) ? 24 : 0));
  }

  // Ages the counts to `localDay`. Never moves backwards.
  static void decayTo(HabitState& s, uint32_t localDay) {
    if (localDay > 0xFFFFU) localDay = 0xFFFFU;
    if (s.lastDay == 0 || localDay <= s.lastDay) {
      if (s.lastDay == 0) s.lastDay = uint16_t(localDay);
      return;
    }
    uint32_t days = localDay - s.lastDay;
    uint32_t factor = 65536U;
    for (; days > 0 && factor > 0; --days)
      factor = uint32_t((uint64_t(factor) * kLambdaQ16 + 32768U) >> 16);
    for (uint8_t i = 0; i < HabitState::kSlots; ++i)
      s.counts[i] = uint16_t((uint32_t(s.counts[i]) * factor + 32768U) >> 16);
    s.lastDay = uint16_t(localDay);
  }

  static uint32_t evidenceQ8(const HabitState& s) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < HabitState::kSlots; ++i) sum += s.counts[i];
    return sum;
  }

  // P(slot) in Q16.
  static uint32_t probabilityQ16(const HabitState& s, uint8_t slotIndex) {
    const uint32_t total =
        evidenceQ8(s) + kKappaQ8 * HabitState::kSlots;
    const uint32_t mass = uint32_t(s.counts[slotIndex % HabitState::kSlots]) + kKappaQ8;
    return uint32_t((uint64_t(mass) << 16) / total);
  }

  // One interaction session started at trusted local time.
  static void observe(HabitState& s, uint32_t localDay, uint32_t minuteOfDay) {
    decayTo(s, localDay);
    const uint8_t observed = slot(localDay, minuteOfDay);
    // Prequential score before the update (multiclass Brier, Q16).
    uint64_t sumSquares = 0;  // Σ p² in Q32
    for (uint8_t i = 0; i < HabitState::kSlots; ++i) {
      const uint64_t p = probabilityQ16(s, i);
      sumSquares += p * p;
    }
    const int64_t pObserved = probabilityQ16(s, observed);
    const int64_t brierModel =
        int64_t(sumSquares >> 16) - 2 * pObserved + 65536;
    const int64_t brierUniform = 65536 - 65536 / HabitState::kSlots;
    const int32_t gain = int32_t(brierUniform - brierModel);
    s.gainQ16 += (gain - s.gainQ16) >> kGainShift;
    // Where in the hour the owner usually arrives (EMA 1/4) [P].
    const int32_t inHour = int32_t(minuteOfDay % 60);
    if (s.counts[observed] == 0) s.minutes[observed] = uint8_t(inHour);
    else s.minutes[observed] = uint8_t(int32_t(s.minutes[observed]) +
                                       (inHour - int32_t(s.minutes[observed])) / 4);
    const uint32_t next = uint32_t(s.counts[observed]) + 256U;
    s.counts[observed] = uint16_t(next > 0xFFFFU ? 0xFFFFU : next);
    if (s.sessions < 0xFFFFU) ++s.sessions;
  }

  // Evidence and a measured gain over the uniform baseline.
  static bool ready(const HabitState& s) {
    return evidenceQ8(s) >= kMinEvidenceQ8 && s.gainQ16 > kGainMarginQ16;
  }

  // P(hour | weekday or weekend) in Q16: "at weekends the owner comes at 9"
  // must not be diluted by the five weekdays.
  static uint32_t withinDayTypeQ16(const HabitState& s, uint8_t slotIndex) {
    const uint8_t base = slotIndex % HabitState::kSlots >= 24 ? 24 : 0;
    uint32_t total = kKappaQ8 * 24;
    for (uint8_t i = 0; i < 24; ++i) total += s.counts[base + i];
    const uint32_t mass = uint32_t(s.counts[slotIndex % HabitState::kSlots]) + kKappaQ8;
    return uint32_t((uint64_t(mass) << 16) / total);
  }

  // An hour is likely when it is ≥ kExpectRatio × uniform within its day type.
  // (Not named `likely`: ESP-IDF defines likely()/unlikely() as macros.)
  static bool isLikely(const HabitState& s, uint8_t slotIndex) {
    return withinDayTypeQ16(s, slotIndex) * 24 >= kExpectRatio * 65536U;
  }

  // Status card: the usual arrival on a weekday or a weekend day, from the
  // most frequent likely hour of that day type and its mean minute. False
  // until ready() and while no hour of that day type is likely.
  static bool usualArrival(const HabitState& s, bool weekendDay, uint8_t& hour,
                           uint8_t& minute) {
    if (!ready(s)) return false;
    const uint8_t base = weekendDay ? 24 : 0;
    bool found = false;
    for (uint8_t h = 0; h < 24; ++h) {
      const uint8_t i = uint8_t(base + h);
      if (!isLikely(s, i)) continue;
      if (!found || s.counts[i] > s.counts[base + hour]) { hour = h; found = true; }
    }
    if (found) minute = s.minutes[base + hour] < 60 ? s.minutes[base + hour] : 0;
    return found;
  }

  // Anticipation: the run of likely hours around (now + lead) gives an
  // expected arrival minute (count-weighted hour start + mean minute). True
  // once that arrival is at most kLeadMinutes away and still ahead; `key`
  // identifies the run (day × slot of its first hour) so the caller acts once.
  static bool expects(const HabitState& s, uint32_t localDay, uint32_t minuteOfDay,
                      uint32_t& key) {
    if (!ready(s)) return false;
    // Work in minutes from the start of `localDay` (may reach the next day).
    const int32_t now = int32_t(minuteOfDay);
    const int32_t probe = now + int32_t(kLeadMinutes);
    int32_t first = probe / 60;  // hour index from the start of localDay
    if (!isLikely(s, slotAtHour(localDay, first))) return false;
    while (first > probe / 60 - 3 && isLikely(s, slotAtHour(localDay, first - 1))) --first;
    int32_t last = probe / 60;
    while (last < probe / 60 + 3 && isLikely(s, slotAtHour(localDay, last + 1))) ++last;
    uint64_t weight = 0, weighted = 0;
    for (int32_t h = first; h <= last; ++h) {
      const uint8_t index = slotAtHour(localDay, h);
      weight += s.counts[index];
      weighted += uint64_t(s.counts[index]) * uint64_t(h * 60 + s.minutes[index]);
    }
    if (weight == 0) return false;
    const int32_t arrival = int32_t(weighted / weight);
    if (arrival <= now || arrival - now > int32_t(kLeadMinutes)) return false;
    key = (localDay + uint32_t(first) / 24) * HabitState::kSlots +
          slotAtHour(localDay, first);
    return true;
  }

 private:
  // Slot of hour `h` counted from the start of `localDay` (h may be < 0 or
  // ≥ 24 for the neighbouring days).
  static uint8_t slotAtHour(uint32_t localDay, int32_t h) {
    const int32_t dayShift = h < 0 ? -1 : h / 24;
    const uint32_t day = uint32_t(int32_t(localDay) + dayShift);
    const uint32_t hour = uint32_t((h % 24 + 24) % 24);
    return slot(day, hour * 60);
  }

};

}  // namespace xuna
