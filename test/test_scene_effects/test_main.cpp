#include <unity.h>
#include <xuna/HoldButton.h>
#include <xuna/visual/SceneEffects.h>
using namespace xuna;

void hold_button_short_press_on_release() {
  HoldButton b;
  TEST_ASSERT_FALSE(b.update(true, 100).shortPress);
  TEST_ASSERT_FALSE(b.update(true, 500).hold);
  const HoldEvents e = b.update(false, 600);
  TEST_ASSERT_TRUE(e.shortPress);
  TEST_ASSERT_FALSE(e.hold);
  TEST_ASSERT_FALSE(b.update(false, 700).shortPress);
}

void hold_button_hold_fires_once_and_suppresses_short() {
  HoldButton b;
  b.update(true, 0);
  TEST_ASSERT_FALSE(b.update(true, HoldButton::kHoldMs - 1).hold);
  TEST_ASSERT_TRUE(b.update(true, HoldButton::kHoldMs).hold);
  TEST_ASSERT_FALSE(b.update(true, 5000).hold);
  const HoldEvents e = b.update(false, 5100);
  TEST_ASSERT_FALSE(e.shortPress);
  TEST_ASSERT_FALSE(e.hold);
  b.update(true, 6000);  // next press starts fresh
  TEST_ASSERT_TRUE(b.update(false, 6100).shortPress);
}

void hold_button_wraps_millis() {
  HoldButton b;
  b.update(true, UINT32_MAX - 100);
  TEST_ASSERT_TRUE(b.update(true, HoldButton::kHoldMs - 101).hold);
}

void scene_mode_cycles_and_maps_day_parts() {
  SceneMode m = SceneMode::Auto;
  const SceneMode order[] = {SceneMode::Day, SceneMode::Evening, SceneMode::Night,
                             SceneMode::Rain, SceneMode::Auto};
  for (SceneMode expected : order) {
    m = nextSceneMode(m);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(expected), uint8_t(m));
  }
  TEST_ASSERT_EQUAL_UINT8(uint8_t(SceneMode::Auto), uint8_t(sceneModeFrom(5)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(SceneMode::Rain), uint8_t(sceneModeFrom(4)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Day), uint8_t(backdropFor(SceneMode::Auto, DayPart::Unknown)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Day), uint8_t(backdropFor(SceneMode::Auto, DayPart::Morning)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Day), uint8_t(backdropFor(SceneMode::Auto, DayPart::Afternoon)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Evening), uint8_t(backdropFor(SceneMode::Auto, DayPart::Evening)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Night), uint8_t(backdropFor(SceneMode::Auto, DayPart::Night)));
  // A fixed mode ignores the clock.
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Rain), uint8_t(backdropFor(SceneMode::Rain, DayPart::Morning)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(Backdrop::Day), uint8_t(backdropFor(SceneMode::Day, DayPart::Night)));
}

void effect_rises_blinks_and_ends() {
  ReactionEffect fx;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EffectKind::None), uint8_t(fx.sample(0).kind));
  fx.start(EffectKind::Heart, 1000);
  EffectSample s = fx.sample(1000);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EffectKind::Heart), uint8_t(s.kind));
  TEST_ASSERT_EQUAL_UINT8(0, s.rise);
  TEST_ASSERT_TRUE(s.visible);
  TEST_ASSERT_EQUAL_UINT8(5, fx.sample(1000 + 5 * ReactionEffect::kRiseStepMs).rise);
  TEST_ASSERT_EQUAL_UINT8(ReactionEffect::kMaxRise, fx.sample(1000 + ReactionEffect::kMaxRise * ReactionEffect::kRiseStepMs).rise);
  TEST_ASSERT_FALSE(fx.sample(1000 + ReactionEffect::kBlinkFromMs + ReactionEffect::kBlinkMs).visible);
  TEST_ASSERT_TRUE(fx.sample(1000 + ReactionEffect::kBlinkFromMs + 2 * ReactionEffect::kBlinkMs).visible);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EffectKind::None),
                          uint8_t(fx.sample(1000 + ReactionEffect::kDurationMs).kind));
  // A new effect replaces a running one and restarts its rise.
  fx.start(EffectKind::Puff, 3000);
  fx.start(EffectKind::Sparkle, 3400);
  s = fx.sample(3400);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EffectKind::Sparkle), uint8_t(s.kind));
  TEST_ASSERT_EQUAL_UINT8(0, s.rise);
}

void rain_stays_inside_the_glass_and_catches_up_boundedly() {
  Pcg32 random(7, 54);
  WindowWeather w;
  w.begin(67, 73, 0, random);
  TEST_ASSERT_FALSE(w.stepRain(WindowWeather::kRainStepMs - 1, random));
  TEST_ASSERT_TRUE(w.stepRain(WindowWeather::kRainStepMs, random));
  for (uint32_t t = 2; t < 2000; ++t) {
    w.stepRain(t * WindowWeather::kRainStepMs, random);
    for (uint8_t i = 0; i < WindowWeather::kDrops; ++i) {
      TEST_ASSERT_LESS_THAN_UINT8(67, w.drops()[i].x);
      TEST_ASSERT_LESS_THAN_UINT8(73, w.drops()[i].y);
    }
  }
  // A long stall (and a millis() wrap) only advances a bounded number of steps.
  TEST_ASSERT_TRUE(w.stepRain(UINT32_MAX - 5, random));
  TEST_ASSERT_TRUE(w.stepRain(400, random));
}

void stars_twinkle_one_at_a_time() {
  Pcg32 random(3, 54);
  WindowWeather w;
  w.begin(67, 73, 0, random);
  TEST_ASSERT_EQUAL_UINT8(0xff, w.stars());
  TEST_ASSERT_FALSE(w.stepStars(WindowWeather::kTwinkleMs - 1, 6, random));
  TEST_ASSERT_TRUE(w.stepStars(WindowWeather::kTwinkleMs, 6, random));
  uint8_t changed = uint8_t(w.stars() ^ 0xff);
  TEST_ASSERT_EQUAL_UINT8(1, __builtin_popcount(changed));
  TEST_ASSERT_TRUE(changed < (1u << 6));
  TEST_ASSERT_FALSE(w.stepStars(WindowWeather::kTwinkleMs + 10, 0, random));
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(hold_button_short_press_on_release);
  RUN_TEST(hold_button_hold_fires_once_and_suppresses_short);
  RUN_TEST(hold_button_wraps_millis);
  RUN_TEST(scene_mode_cycles_and_maps_day_parts);
  RUN_TEST(effect_rises_blinks_and_ends);
  RUN_TEST(rain_stays_inside_the_glass_and_catches_up_boundedly);
  RUN_TEST(stars_twinkle_one_at_a_time);
  return UNITY_END();
}
