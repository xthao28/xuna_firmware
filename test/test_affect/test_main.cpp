#include <unity.h>

#include <xuna/base/Random.h>
#include <xuna/life/Affect.h>

using namespace xuna;

namespace {

const uint32_t kMin = Affect::kMinuteMs;

TemperamentState withOffset(TraitId trait, int16_t value) {
  TemperamentState g;
  g.offset[static_cast<uint8_t>(trait)] = value;
  return g;
}

// Field by field: AffectState has a padding byte.
bool same(const AffectState& a, const AffectState& b) {
  return a.emotionValence == b.emotionValence && a.emotionArousal == b.emotionArousal &&
         a.moodValence == b.moodValence && a.moodArousal == b.moodArousal &&
         a.competenceQ16 == b.competenceQ16 && a.last == b.last;
}

int16_t absolute(int16_t v) { return v < 0 ? int16_t(-v) : v; }

uint32_t minutesToFade(Affect& a, uint32_t start) {
  uint32_t m = 0;
  while (absolute(a.state().emotionValence) >= 819 && m < 600) a.update(start + ++m * kMin, 0);
  return m;
}

}  // namespace

void parameters_follow_the_genome() {
  const AffectParameters neutral = Affect::parametersFor(TemperamentState());
  TEST_ASSERT_EQUAL_INT16(8192, neutral.gain);
  TEST_ASSERT_EQUAL_INT16(12288, neutral.retain);
  TEST_ASSERT_EQUAL_INT16(16384, neutral.massQ14);
  TEST_ASSERT_EQUAL_INT16(0, neutral.prevalence);
  const AffectParameters sensitive =
      Affect::parametersFor(withOffset(TraitId::Sensitivity, kOffsetRange));
  TEST_ASSERT_EQUAL_INT16(8192 * (16384 + 9830) / 16384, sensitive.gain);
  TEST_ASSERT_EQUAL_INT16(12288 + 9830 / 4, sensitive.retain);
  const AffectParameters heavy =
      Affect::parametersFor(withOffset(TraitId::MoodInertia, kOffsetRange));
  TEST_ASSERT_EQUAL_INT16(16384 + 9830 / 2, heavy.massQ14);
  TemperamentState sunny;
  sunny.baseline[uint8_t(TraitId::MoodBaseline)] = kBaselineRange;
  TEST_ASSERT_EQUAL_INT16(kBaselineRange / 4, Affect::parametersFor(sunny).prevalence);
  Affect a(Affect::parametersFor(sunny));
  TEST_ASSERT_EQUAL_INT16(kBaselineRange / 4, a.state().moodValence);  // starts at its baseline
}

void emotions_come_from_expectation() {
  Affect a;
  // p̂ 0.2 → unexpected pet: joy. Size = gain × (1 + surprise) / 2.
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Appraisal::Joy), uint8_t(a.feedback(true, 13107)));
  const int16_t joy = a.state().emotionValence;
  TEST_ASSERT_EQUAL_INT16(8192 * (16384 + (16384 - 3276)) / 32768, joy);
  Affect b;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Appraisal::Satisfaction), uint8_t(b.feedback(true, 58982)));
  TEST_ASSERT_TRUE(b.state().emotionValence > 0 && b.state().emotionValence < joy);
  TEST_ASSERT_TRUE(b.state().emotionArousal < a.state().emotionArousal);  // less surprise
  Affect c, d;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Appraisal::Disappointment), uint8_t(c.feedback(false, 58982)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Appraisal::Distress), uint8_t(d.feedback(false, 13107)));
  TEST_ASSERT_TRUE(c.state().emotionValence < d.state().emotionValence);
  TEST_ASSERT_TRUE(d.state().emotionValence < 0);
}

void emotions_fade_and_mood_returns_home() {
  Affect a;
  for (int i = 0; i < 6; ++i) a.feedback(true, 13107);
  a.update(5 * kMin, 0);
  TEST_ASSERT_TRUE(a.state().moodValence > 0);
  a.update(12 * 60 * kMin, 0);
  TEST_ASSERT_EQUAL_INT16(0, a.state().emotionValence);
  TEST_ASSERT_EQUAL_INT16(0, a.state().emotionArousal);
  TEST_ASSERT_TRUE(absolute(a.state().moodValence) <= 64);
}

void mood_has_a_floor_and_absence_is_never_sad() {
  Affect a(Affect::parametersFor(withOffset(TraitId::Sensitivity, kOffsetRange)));
  uint32_t now = 0;
  for (int minute = 0; minute < 180; ++minute) {
    for (int i = 0; i < 5; ++i) a.feedback(false, 65535);  // a terrible day
    now += kMin;
    a.update(now, 0);
    TEST_ASSERT_TRUE(a.state().moodValence >= Affect::kMoodFloor);
  }
  TEST_ASSERT_EQUAL_INT16(Affect::kMoodFloor, a.state().moodValence);
  // Then 30 days with nobody around: back to its baseline, not lower.
  for (int day = 1; day <= 30; ++day) a.update(now + day * 1440U * kMin, 0);
  TEST_ASSERT_EQUAL_INT16(0, a.state().emotionValence);
  TEST_ASSERT_TRUE(absolute(a.state().moodValence) <= 64);
  // The owner coming back is always welcome.
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Appraisal::Welcome), uint8_t(a.ownerArrived(false)));
  TEST_ASSERT_TRUE(a.state().emotionValence > 0);
}

void sensitivity_makes_emotions_bigger_and_longer() {
  Affect calm(Affect::parametersFor(withOffset(TraitId::Sensitivity, -kOffsetRange)));
  Affect touchy(Affect::parametersFor(withOffset(TraitId::Sensitivity, kOffsetRange)));
  calm.feedback(false, 58982);
  touchy.feedback(false, 58982);
  TEST_ASSERT_TRUE(touchy.state().emotionValence < calm.state().emotionValence);
  const uint32_t calmMinutes = minutesToFade(calm, 0);
  const uint32_t touchyMinutes = minutesToFade(touchy, 0);
  TEST_ASSERT_TRUE(touchyMinutes > 2 * calmMinutes);
}

void inertia_slows_the_mood() {
  Affect light(Affect::parametersFor(withOffset(TraitId::MoodInertia, -kOffsetRange)));
  Affect heavy(Affect::parametersFor(withOffset(TraitId::MoodInertia, kOffsetRange)));
  light.feedback(true, 13107);
  heavy.feedback(true, 13107);
  light.update(3 * kMin, 0);
  heavy.update(3 * kMin, 0);
  TEST_ASSERT_TRUE(light.state().moodValence > heavy.state().moodValence);
  TEST_ASSERT_TRUE(heavy.state().moodValence > 0);
}

void competence_tracks_prediction_quality() {
  Affect good, bad;
  for (int i = 0; i < 64; ++i) {
    good.feedback(true, 62259);   // expected 0.95, petted
    bad.feedback(i % 2 == 0, 32768);  // coin flips at 0.5
  }
  TEST_ASSERT_TRUE(good.state().competenceQ16 > 58000);
  TEST_ASSERT_TRUE(bad.state().competenceQ16 > 31000 && bad.state().competenceQ16 < 34500);
  Affect wrong;
  for (int i = 0; i < 64; ++i) wrong.feedback(false, 62259);  // confident and wrong
  TEST_ASSERT_TRUE(wrong.state().competenceQ16 < 8000);
}

void boredom_lowers_resting_arousal() {
  Affect a;
  a.update(600 * kMin, 65535);
  TEST_ASSERT_TRUE(a.state().moodArousal < -3000);
  a.update(1200 * kMin, 0);
  TEST_ASSERT_TRUE(absolute(a.state().moodArousal) <= 64);
}

void split_updates_and_long_gaps_are_exact() {
  Affect whole, split;
  whole.reset(0xfff00000U);
  split.reset(0xfff00000U);
  whole.feedback(true, 13107);
  split.feedback(true, 13107);
  Pcg32 random(4, 2);
  uint32_t now = 0xfff00000U;
  const uint32_t end = now + 6 * 60 * kMin;
  while (now != end) {
    uint32_t step = 1 + random.below(5 * kMin);
    if (uint32_t(end - now) < step) step = end - now;
    now += step;
    split.update(now, 20000);
  }
  whole.update(end, 20000);
  TEST_ASSERT_TRUE(same(whole.state(), split.state()));

  // A 5-day gap lands where minute-by-minute lands, for extreme genomes too.
  for (int16_t trait = -kOffsetRange; trait <= kOffsetRange; trait += kOffsetRange) {
    TemperamentState g;
    for (uint8_t i = 0; i < kTraitCount; ++i) g.offset[i] = trait;
    Affect gap(Affect::parametersFor(g)), daily(Affect::parametersFor(g));
    gap.feedback(false, 65535);
    daily.feedback(false, 65535);
    gap.update(5 * 1440 * kMin, 30000);
    for (uint32_t m = 1; m <= 5 * 1440; ++m) daily.update(m * kMin, 30000);
    TEST_ASSERT_TRUE(same(gap.state(), daily.state()));
  }
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(parameters_follow_the_genome);
  RUN_TEST(emotions_come_from_expectation);
  RUN_TEST(emotions_fade_and_mood_returns_home);
  RUN_TEST(mood_has_a_floor_and_absence_is_never_sad);
  RUN_TEST(sensitivity_makes_emotions_bigger_and_longer);
  RUN_TEST(inertia_slows_the_mood);
  RUN_TEST(competence_tracks_prediction_quality);
  RUN_TEST(boredom_lowers_resting_arousal);
  RUN_TEST(split_updates_and_long_gaps_are_exact);
  return UNITY_END();
}
