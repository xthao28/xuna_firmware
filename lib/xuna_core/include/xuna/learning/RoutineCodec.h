#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/learning/HabitPredictor.h>

namespace xuna {

struct RoutineProjection {
  bool ready = false;
  bool weekdayKnown = false;
  bool weekendKnown = false;
  bool anticipated = false;
  uint8_t weekdayHour = 0;
  uint8_t weekdayMinute = 0;
  uint8_t weekendHour = 0;
  uint8_t weekendMinute = 0;
  uint8_t sessions = 0;
};

// Read-only BLE projection (characteristic 7a6e0014, capability byte 5 bit 2)
// of what HabitPredictor learned about when the owner usually visits.
// 8 bytes:
//   0: version = 1
//   1: size = 8
//   2: flags (bit0 ready, bit1 weekdayKnown, bit2 weekendKnown, bit3 anticipated)
//   3: weekdayHour (0..23; 0 when !weekdayKnown)
//   4: weekdayMinute (0..59; 0 when !weekdayKnown)
//   5: weekendHour (0..23; 0 when !weekendKnown)
//   6: weekendMinute (0..59; 0 when !weekendKnown)
//   7: observed sessions (saturated at 255)
class RoutineCodec {
 public:
  static constexpr size_t kSize = 8;

  static RoutineProjection fromHabit(const HabitState& habit,
                                     bool anticipated = false) {
    RoutineProjection p;
    p.ready = HabitPredictor::ready(habit);
    p.weekdayKnown = HabitPredictor::usualArrival(
        habit, false, p.weekdayHour, p.weekdayMinute);
    if (!p.weekdayKnown) {
      p.weekdayHour = 0;
      p.weekdayMinute = 0;
    }
    p.weekendKnown = HabitPredictor::usualArrival(
        habit, true, p.weekendHour, p.weekendMinute);
    if (!p.weekendKnown) {
      p.weekendHour = 0;
      p.weekendMinute = 0;
    }
    p.anticipated = p.ready && anticipated;
    p.sessions = habit.sessions > 255U ? 255U : static_cast<uint8_t>(habit.sessions);
    return p;
  }

  static bool encode(const RoutineProjection& value, uint8_t* out) {
    if (!out || !valid(value)) return false;
    out[0] = 1;
    out[1] = kSize;
    uint8_t flags = 0;
    if (value.ready) flags |= 1U << 0;
    if (value.weekdayKnown) flags |= 1U << 1;
    if (value.weekendKnown) flags |= 1U << 2;
    if (value.anticipated) flags |= 1U << 3;
    out[2] = flags;
    out[3] = value.weekdayKnown ? value.weekdayHour : 0;
    out[4] = value.weekdayKnown ? value.weekdayMinute : 0;
    out[5] = value.weekendKnown ? value.weekendHour : 0;
    out[6] = value.weekendKnown ? value.weekendMinute : 0;
    out[7] = value.sessions;
    return true;
  }

  static bool valid(const RoutineProjection& value) {
    if (!value.ready && (value.weekdayKnown || value.weekendKnown || value.anticipated))
      return false;
    if (value.weekdayKnown && (value.weekdayHour > 23 || value.weekdayMinute > 59))
      return false;
    if (!value.weekdayKnown && (value.weekdayHour != 0 || value.weekdayMinute != 0))
      return false;
    if (value.weekendKnown && (value.weekendHour > 23 || value.weekendMinute > 59))
      return false;
    if (!value.weekendKnown && (value.weekendHour != 0 || value.weekendMinute != 0))
      return false;
    return true;
  }
};

}  // namespace xuna

