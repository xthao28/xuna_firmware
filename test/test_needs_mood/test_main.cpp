#include <unity.h>
#include <xuna/life/Needs.h>

using namespace xuna;

void catch_up_is_partition_invariant() {
  LifeTuning tuning;
  tuning.initialEnergy = 60000;
  tuning.initialStimulation = 50000;
  tuning.energyDecayPerMinute = 6000;
  tuning.stimulationDecayPerMinute = 3000;
  Needs one(tuning), stepped(tuning);
  one.reset(UINT32_MAX - 100);
  stepped.reset(0);
  one.update(100);
  stepped.update(200);
  TEST_ASSERT_EQUAL_UINT16(one.state().energy, stepped.state().energy);
  TEST_ASSERT_EQUAL_UINT16(one.state().stimulation, stepped.state().stimulation);
  Needs gap(tuning), pieces(tuning);
  gap.update(180000);
  pieces.update(60000);
  pieces.update(120000);
  TEST_ASSERT_EQUAL_UINT16(gap.state().energy, pieces.state().energy);
  TEST_ASSERT_EQUAL_UINT16(gap.state().stimulation, pieces.state().stimulation);
}

void cold_boot_has_no_offline_catch_up() {
  LifeTuning tuning;
  tuning.initialEnergy = 60000;
  tuning.initialStimulation = 50000;
  tuning.energyDecayPerMinute = 6000;
  tuning.stimulationDecayPerMinute = 3000;
  Needs needs(tuning);
  needs.update(60000);
  TEST_ASSERT_NOT_EQUAL_UINT16(tuning.initialEnergy, needs.state().energy);
  needs.reset(1000000);
  TEST_ASSERT_EQUAL_UINT16(tuning.initialEnergy, needs.state().energy);
  TEST_ASSERT_EQUAL_UINT16(tuning.initialStimulation, needs.state().stimulation);
  needs.update(1000001);
  TEST_ASSERT_EQUAL_UINT16(tuning.initialEnergy, needs.state().energy);
}

void decay_clamps_and_wraps() {
  LifeTuning tuning;
  tuning.initialEnergy = 100;
  tuning.initialStimulation = 50;
  tuning.energyDecayPerMinute = 60000;
  tuning.stimulationDecayPerMinute = 60000;
  Needs needs(tuning);
  needs.reset(UINT32_MAX - 100);
  needs.update(100);
  TEST_ASSERT_EQUAL_UINT16(0, needs.state().energy);
  TEST_ASSERT_EQUAL_UINT16(0, needs.state().stimulation);
  TEST_ASSERT_EQUAL_UINT16(0, needs.energy().raw());
}

void mood_hysteresis() {
  Mood mood;
  NeedsState needs;
  needs.energy = 12000;
  needs.stimulation = 1000;
  mood.update(needs);
  TEST_ASSERT_EQUAL(MoodLabel::Sleepy, mood.label());
  needs.energy = 20000;
  mood.update(needs);
  TEST_ASSERT_EQUAL(MoodLabel::Sleepy, mood.label());
  needs.energy = 26000;
  mood.update(needs);
  TEST_ASSERT_EQUAL(MoodLabel::Calm, mood.label());
  needs.energy = 50000;
  needs.stimulation = 60000;
  mood.update(needs);
  TEST_ASSERT_EQUAL(MoodLabel::Overwhelmed, mood.label());
  needs.stimulation = 50000;
  mood.update(needs);
  TEST_ASSERT_EQUAL(MoodLabel::Overwhelmed, mood.label());
  needs.stimulation = 48000;
  mood.update(needs);
  TEST_ASSERT_EQUAL(MoodLabel::Calm, mood.label());
}

void mood_axes_clamp() {
  Mood mood;
  NeedsState needs;
  needs.energy = 65535;
  needs.stimulation = 65535;
  MoodSignals signals;
  signals.interaction = true;
  signals.positiveFeedback = true;
  signals.valenceDelta = 32767;
  signals.arousalDelta = 32767;
  mood.update(needs, signals);
  TEST_ASSERT_EQUAL_UINT16(65535, mood.state().arousal);
  TEST_ASSERT_EQUAL_INT16(32767, mood.state().valence);
  signals.negativeFeedback = true;
  signals.valenceDelta = -32768;
  signals.arousalDelta = -32768;
  needs.energy = 0;
  needs.stimulation = 0;
  mood.update(needs, signals);
  TEST_ASSERT_EQUAL_UINT16(0, mood.state().arousal);
  TEST_ASSERT_EQUAL_INT16(-32768, mood.state().valence);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(catch_up_is_partition_invariant);
  RUN_TEST(cold_boot_has_no_offline_catch_up);
  RUN_TEST(decay_clamps_and_wraps);
  RUN_TEST(mood_hysteresis);
  RUN_TEST(mood_axes_clamp);
  return UNITY_END();
}