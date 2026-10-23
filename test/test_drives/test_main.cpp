#include <cstring>

#include <unity.h>

#include <xuna/base/Random.h>
#include <xuna/life/Drives.h>

using namespace xuna;

namespace {

const uint32_t kMin = Drives::kMinuteMs;
const uint16_t kAwake = 65535;

bool same(const DriveState& a, const DriveState& b) {
  return std::memcmp(&a, &b, sizeof(DriveState)) == 0;
}

TemperamentState genome(int16_t value) {
  TemperamentState g;
  for (uint8_t i = 0; i < kTraitCount; ++i) g.baseline[i] = value;
  return g;
}

}  // namespace

void rates_follow_the_genome() {
  const DriveRates neutral = Drives::ratesFor(genome(0));
  TEST_ASSERT_EQUAL_UINT16(512, neutral.explore);
  TEST_ASSERT_EQUAL_UINT16(45875, neutral.socialCeiling);
  const DriveRates high = Drives::ratesFor(genome(kBaselineRange));
  const DriveRates low = Drives::ratesFor(genome(-kBaselineRange));
  TEST_ASSERT_EQUAL_UINT16(588, high.explore);  // 512 × 1.15
  TEST_ASSERT_EQUAL_UINT16(435, low.explore);   // 512 × 0.85
  TEST_ASSERT_TRUE(high.play > neutral.play && neutral.play > low.play);
  TEST_ASSERT_TRUE(high.socialCeiling > neutral.socialCeiling);
  TEST_ASSERT_TRUE(high.boredom > low.boredom);
  // Fully learned extremes stay usable: never negative, never above 1.
  TemperamentState extreme = genome(kBaselineRange);
  for (uint8_t i = 0; i < kTraitCount; ++i) extreme.offset[i] = kOffsetRange;
  TEST_ASSERT_EQUAL_UINT16(65535, Drives::ratesFor(extreme).socialCeiling);
}

void split_updates_equal_one_update() {
  Drives whole, split;
  whole.reset(0xfff00000U);  // crosses the uint32 millis() wrap
  split.reset(0xfff00000U);
  whole.ownerContact();
  split.ownerContact();
  Pcg32 random(9, 1);
  uint32_t now = 0xfff00000U;
  const uint32_t end = now + 10 * 60 * kMin;
  while (now != end) {
    uint32_t step = 1 + random.below(7 * kMin);
    if (uint32_t(end - now) < step) step = end - now;
    now += step;
    split.update(now, kAwake, false);
  }
  whole.update(end, kAwake, false);
  TEST_ASSERT_TRUE(same(whole.state(), split.state()));
  TEST_ASSERT_EQUAL_UINT16(whole.minutesSinceContact(), split.minutesSinceContact());
}

void explore_rises_and_curiosity_satisfies_it() {
  Drives d;
  d.reset(0);
  d.update(30 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(30 * 512, d.state().explore);
  d.update(30 * kMin + 59999, kAwake, false);  // not a whole minute yet
  TEST_ASSERT_EQUAL_UINT16(30 * 512, d.state().explore);
  d.acted(ActionId::Curious);
  TEST_ASSERT_EQUAL_UINT16(0, d.state().explore);
  d.update(300 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(65535, d.state().explore);
  d.acted(ActionId::Curious);
  TEST_ASSERT_EQUAL_UINT16(32767, d.state().explore);
  // Resting freezes it.
  d.update(400 * kMin, kAwake, true);
  TEST_ASSERT_EQUAL_UINT16(32767, d.state().explore);
}

void play_needs_energy_and_happy_satisfies_it() {
  Drives d;
  d.reset(0);
  d.update(60 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(60 * 384, d.state().play);
  d.update(90 * kMin, 20000, false);  // tired: play fades
  TEST_ASSERT_EQUAL_UINT16(60 * 384 - 30 * 256, d.state().play);
  d.acted(ActionId::Happy);
  TEST_ASSERT_EQUAL_UINT16(0, d.state().play);
}

void social_longs_then_settles_and_never_becomes_sadness() {
  Drives d;
  d.reset(0);
  d.update(600 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(0, d.state().social);  // never met: nothing to miss
  d.ownerContact();
  d.update(600 * kMin + 60 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(60 * 512, d.state().social);
  d.update(600 * kMin + 120 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(45875, d.state().social);  // capped
  d.update(600 * kMin + 150 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(45875 - 30 * 512, d.state().social);
  // Three days away: back to calm, not pining.
  for (uint32_t day = 1; day <= 3; ++day) d.update(600 * kMin + day * 1440 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(0, d.state().social);
  TEST_ASSERT_EQUAL_UINT16(150 + 1290 + 2 * 1440, d.minutesSinceContact());

  // A sociable genome misses the owner more than a reserved one.
  Drives warm(Drives::ratesFor(genome(kBaselineRange)));
  Drives shy(Drives::ratesFor(genome(-kBaselineRange)));
  warm.reset(0);
  shy.reset(0);
  warm.ownerContact();
  shy.ownerContact();
  warm.update(120 * kMin, kAwake, false);
  shy.update(120 * kMin, kAwake, false);
  TEST_ASSERT_TRUE(warm.state().social > shy.state().social);
}

void boredom_needs_something_new() {
  Drives d;
  d.reset(0);
  d.update(100 * kMin, kAwake, false);
  TEST_ASSERT_EQUAL_UINT16(100 * 256, d.state().boredom);
  d.acted(ActionId::Idle);  // first action is new
  TEST_ASSERT_EQUAL_UINT16(100 * 256 - 8192, d.state().boredom);
  d.acted(ActionId::Idle);  // the same again is not
  TEST_ASSERT_EQUAL_UINT16(100 * 256 - 8192, d.state().boredom);
  d.acted(ActionId::Happy);
  TEST_ASSERT_EQUAL_UINT16(100 * 256 - 16384, d.state().boredom);
  d.ownerContact();
  TEST_ASSERT_EQUAL_UINT16(0, d.state().boredom);
  d.update(200 * kMin, kAwake, true);  // asleep: no boredom
  TEST_ASSERT_EQUAL_UINT16(0, d.state().boredom);
}

void long_gap_equals_day_by_day_for_every_genome() {
  for (int16_t trait = -kTraitOne; trait <= kTraitOne; trait += 4096) {
    TemperamentState g = genome(0);
    for (uint8_t i = 0; i < kTraitCount; ++i) g.offset[i] = trait;  // via offset: ±1 reachable
    const DriveRates rates = Drives::ratesFor(g);
    Drives gap(rates), daily(rates);
    gap.reset(0);
    daily.reset(0);
    gap.ownerContact();
    daily.ownerContact();
    gap.update(5 * 1440 * kMin, kAwake, false);
    for (uint32_t m = 1; m <= 5 * 1440; ++m) daily.update(m * kMin, kAwake, false);
    TEST_ASSERT_TRUE(same(gap.state(), daily.state()));
  }
}

void two_genomes_live_differently() {
  // Same day, same owner contact: a curious pet wants to explore sooner.
  TemperamentState a, b;
  Temperament::seed(a, 1);
  Temperament::seed(b, 2);
  a.baseline[uint8_t(TraitId::Curiosity)] = kBaselineRange;
  b.baseline[uint8_t(TraitId::Curiosity)] = -kBaselineRange;
  Drives da(Drives::ratesFor(a)), db(Drives::ratesFor(b));
  da.reset(0);
  db.reset(0);
  uint32_t ta = 0, tb = 0;
  for (uint32_t m = 1; m <= 240 && (!ta || !tb); ++m) {
    da.update(m * kMin, kAwake, false);
    db.update(m * kMin, kAwake, false);
    if (!ta && da.state().explore >= 49152) ta = m;
    if (!tb && db.state().explore >= 49152) tb = m;
  }
  TEST_ASSERT_EQUAL_UINT32(84, ta);   // 49152 / 588
  TEST_ASSERT_EQUAL_UINT32(113, tb);  // 49152 / 435
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(rates_follow_the_genome);
  RUN_TEST(split_updates_equal_one_update);
  RUN_TEST(explore_rises_and_curiosity_satisfies_it);
  RUN_TEST(play_needs_energy_and_happy_satisfies_it);
  RUN_TEST(social_longs_then_settles_and_never_becomes_sadness);
  RUN_TEST(boredom_needs_something_new);
  RUN_TEST(long_gap_equals_day_by_day_for_every_genome);
  RUN_TEST(two_genomes_live_differently);
  return UNITY_END();
}
