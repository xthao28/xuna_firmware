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
  pieces.update(180000);
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

void recovery_rises_clamps_and_is_partition_invariant() {
  LifeTuning tuning;
  tuning.initialEnergy = 10000;
  tuning.initialStimulation = 50000;
  tuning.energyRecoveryPerMinute = 6000;
  tuning.stimulationDecayPerMinute = 3000;
  Needs gap(tuning), pieces(tuning);
  gap.update(180000, true);
  pieces.update(45000, true);
  pieces.update(100001, true);
  pieces.update(180000, true);
  TEST_ASSERT_EQUAL_UINT16(28000, gap.state().energy);
  TEST_ASSERT_EQUAL_UINT16(gap.state().energy, pieces.state().energy);
  // Stimulation keeps decaying while energy recovers.
  TEST_ASSERT_EQUAL_UINT16(41000, gap.state().stimulation);
  TEST_ASSERT_EQUAL_UINT16(gap.state().stimulation, pieces.state().stimulation);

  tuning.initialEnergy = 65000;
  Needs full(tuning);
  full.reset(UINT32_MAX - 100);
  full.update(600000, true);
  TEST_ASSERT_EQUAL_UINT16(65535, full.state().energy);

  // Switching back to decay uses the decay rate from the switch point.
  tuning.initialEnergy = 30000;
  tuning.energyDecayPerMinute = 1200;
  Needs mixed(tuning);
  mixed.update(60000, true);
  mixed.update(120000, false);
  TEST_ASSERT_EQUAL_UINT16(30000 + 6000 - 1200, mixed.state().energy);
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

void spend_and_stimulate_are_bounded() {
  Needs needs;
  needs.reset(0);
  needs.spend(4096);
  TEST_ASSERT_EQUAL_UINT16(65535 - 4096, needs.state().energy);
  needs.spend(65535);
  TEST_ASSERT_EQUAL_UINT16(0, needs.state().energy);
  needs.stimulate(40000);
  needs.stimulate(40000);
  TEST_ASSERT_EQUAL_UINT16(65535, needs.state().stimulation);
  // Defaults: interactions neither cost energy nor stimulate.
  TEST_ASSERT_EQUAL_UINT8(0, LifeTuning().actionCostPercent);
  TEST_ASSERT_EQUAL_UINT16(0, LifeTuning().stimulationPerInteraction);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(catch_up_is_partition_invariant);
  RUN_TEST(cold_boot_has_no_offline_catch_up);
  RUN_TEST(decay_clamps_and_wraps);
  RUN_TEST(recovery_rises_clamps_and_is_partition_invariant);
  RUN_TEST(mood_hysteresis);
  RUN_TEST(mood_axes_clamp);
  RUN_TEST(spend_and_stimulate_are_bounded);
  return UNITY_END();
}