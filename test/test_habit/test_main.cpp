#include <cstring>

#include <unity.h>

#include <xuna/learning/HabitPredictor.h>
#include <xuna/lab/BoardLab.h>
#include <xuna/persist/SnapshotCodec.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

namespace {
// Local day index of 2026-10-19 (a Monday) counted from 2020-01-01.
const uint32_t kMonday = 2483;
}  // namespace

void weekday_and_slots_follow_the_2020_epoch() {
  TEST_ASSERT_FALSE(HabitPredictor::weekend(0));        // 2020-01-01 Wednesday
  TEST_ASSERT_TRUE(HabitPredictor::weekend(3));         // Saturday
  TEST_ASSERT_TRUE(HabitPredictor::weekend(4));         // Sunday
  TEST_ASSERT_FALSE(HabitPredictor::weekend(kMonday));
  TEST_ASSERT_TRUE(HabitPredictor::weekend(kMonday + 5));
  TEST_ASSERT_EQUAL_UINT8(19, HabitPredictor::slot(kMonday, 19 * 60 + 5));
  TEST_ASSERT_EQUAL_UINT8(24 + 9, HabitPredictor::slot(kMonday + 6, 9 * 60));
}

void decay_ages_counts_and_never_goes_back() {
  HabitState s;
  HabitPredictor::observe(s, kMonday, 19 * 60);
  TEST_ASSERT_EQUAL_UINT16(256, s.counts[19]);
  TEST_ASSERT_EQUAL_UINT8(0, s.minutes[19]);
  HabitPredictor::decayTo(s, kMonday + 1);
  TEST_ASSERT_EQUAL_UINT16(243, s.counts[19]);  // 256 × 0.95
  HabitPredictor::decayTo(s, kMonday);          // older day: no change
  TEST_ASSERT_EQUAL_UINT16(243, s.counts[19]);
  TEST_ASSERT_EQUAL_UINT16(kMonday + 1, s.lastDay);
}

void probabilities_are_smoothed_and_normalised() {
  HabitState s;
  uint64_t sum = 0;
  for (uint8_t i = 0; i < HabitState::kSlots; ++i) sum += HabitPredictor::probabilityQ16(s, i);
  TEST_ASSERT_UINT32_WITHIN(48, 65536, uint32_t(sum));  // uniform before data
  TEST_ASSERT_FALSE(HabitPredictor::ready(s));
}

void a_consistent_owner_gains_and_a_scattered_one_does_not() {
  HabitState steady, scattered;
  for (uint32_t d = 0; d < 21; ++d) {
    HabitPredictor::observe(steady, kMonday + d, 19 * 60 + (d % 3) * 10);
    HabitPredictor::observe(scattered, kMonday + d, (d * 7 % 24) * 60 + 5);
  }
  TEST_ASSERT_TRUE(HabitPredictor::ready(steady));
  TEST_ASSERT_TRUE(steady.gainQ16 > HabitPredictor::kGainMarginQ16);
  TEST_ASSERT_FALSE(HabitPredictor::ready(scattered));
  // Mean minute within the hour follows the observations (0, 10, 20).
  TEST_ASSERT_UINT8_WITHIN(10, 10, steady.minutes[19]);
}

void expects_fires_shortly_before_the_usual_arrival() {
  HabitState s;
  for (uint32_t d = 0; d < 28; ++d) HabitPredictor::observe(s, kMonday + d, 19 * 60);
  const uint32_t day = kMonday + 28;  // a weekday
  uint32_t key = 0;
  TEST_ASSERT_FALSE(HabitPredictor::expects(s, day, 18 * 60 + 30, key));  // 30 min early
  TEST_ASSERT_TRUE(HabitPredictor::expects(s, day, 18 * 60 + 45, key));
  const uint32_t first = key;
  TEST_ASSERT_TRUE(HabitPredictor::expects(s, day, 18 * 60 + 55, key));
  TEST_ASSERT_EQUAL_UINT32(first, key);  // same run: the core acts once
  TEST_ASSERT_FALSE(HabitPredictor::expects(s, day, 19 * 60 + 5, key));   // already due
  TEST_ASSERT_FALSE(HabitPredictor::expects(s, day, 9 * 60, key));        // other hours
}

void usual_arrival_reports_the_likely_hour_per_day_type() {
  HabitState s;
  uint8_t hour = 0, minute = 0;
  TEST_ASSERT_FALSE(HabitPredictor::usualArrival(s, false, hour, minute));
  for (uint32_t d = 0; d < 28; ++d) {
    const uint32_t day = kMonday + d;
    HabitPredictor::observe(s, day, HabitPredictor::weekend(day) ? 9 * 60 + 40 : 19 * 60 + 10);
  }
  TEST_ASSERT_TRUE(HabitPredictor::usualArrival(s, false, hour, minute));
  TEST_ASSERT_EQUAL_UINT8(19, hour);
  TEST_ASSERT_EQUAL_UINT8(10, minute);
  TEST_ASSERT_TRUE(HabitPredictor::usualArrival(s, true, hour, minute));
  TEST_ASSERT_EQUAL_UINT8(9, hour);
  TEST_ASSERT_EQUAL_UINT8(40, minute);
}

void habit_section_round_trips_and_is_omitted_when_empty() {
  LifeSnapshotV2 s;
  uint8_t bytes[SnapshotCodec::kBudget];
  const size_t without = SnapshotCodec::encode(s, bytes, sizeof(bytes));
  HabitPredictor::observe(s.habit, kMonday, 19 * 60 + 12);
  const size_t with = SnapshotCodec::encode(s, bytes, sizeof(bytes));
  TEST_ASSERT_EQUAL_UINT32(without + SnapshotCodec::kSectionHeaderSize +
                               SnapshotCodec::kHabitSize,
                           with);
  LifeSnapshotV2 back;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(LoadResult::Ok),
                          uint8_t(SnapshotCodec::decode(bytes, with, back).result));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&s.habit, &back.habit, sizeof(HabitState));
}

void no_trusted_time_no_routine_and_forget_clears_it() {
  BoardLab lab;
  lab.store().seedV1(xuna_fixture::scripted, LifeEngine::kSnapshotSize);
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_TRUE(lab.greeting(1000));
  lab.loop(1000);
  TEST_ASSERT_EQUAL_UINT16(0, lab.snapshot().habit.sessions);  // clock unknown
  uint8_t frame[16];
  TEST_ASSERT_EQUAL_UINT8(0, lab.command(frame, BoardLab::timeSync(frame, 1, 1792026000U, 420),
                                         2000));
  // A sync is not presence (the app also syncs every 6 h in the background).
  TEST_ASSERT_EQUAL_UINT16(0, lab.snapshot().habit.sessions);
  const uint32_t later = 2000 + HabitPredictor::kSessionGapMs + 1000;
  for (uint32_t t = 3000; t < later; t += 60000) lab.loop(t);
  lab.loop(later);
  lab.greeting(later);  // a new session with trusted time
  TEST_ASSERT_EQUAL_UINT16(1, lab.snapshot().habit.sessions);
  TEST_ASSERT_EQUAL_UINT8(0, lab.command(frame, BoardLab::forget(frame, 2), later + 1000));
  TEST_ASSERT_EQUAL_UINT16(0, lab.snapshot().habit.sessions);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(weekday_and_slots_follow_the_2020_epoch);
  RUN_TEST(decay_ages_counts_and_never_goes_back);
  RUN_TEST(probabilities_are_smoothed_and_normalised);
  RUN_TEST(usual_arrival_reports_the_likely_hour_per_day_type);
  RUN_TEST(a_consistent_owner_gains_and_a_scattered_one_does_not);
  RUN_TEST(expects_fires_shortly_before_the_usual_arrival);
  RUN_TEST(habit_section_round_trips_and_is_omitted_when_empty);
  RUN_TEST(no_trusted_time_no_routine_and_forget_clears_it);
  return UNITY_END();
}
