#include <unity.h>
#include <xuna/visual/Backlight.h>
using namespace xuna;

void target_by_state_and_card() {
  BacklightDimmer d;
  TEST_ASSERT_EQUAL_UINT8(12, d.target(PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(12, d.target(PetState::Happy, false));
  TEST_ASSERT_EQUAL_UINT8(12, d.target(PetState::Curious, false));
  TEST_ASSERT_EQUAL_UINT8(7, d.target(PetState::Sleep, false));
  TEST_ASSERT_EQUAL_UINT8(16, d.target(PetState::Sleep, true));
  TEST_ASSERT_EQUAL_UINT8(16, d.target(PetState::Idle, true));
}

void levels_are_clamped_to_the_driver_range() {
  BacklightLevels l;
  l.awake = 0; l.asleep = 40; l.card = 16;
  BacklightDimmer d(l);
  TEST_ASSERT_EQUAL_UINT8(1, d.target(PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(16, d.target(PetState::Sleep, false));
}

void fades_one_step_per_period() {
  BacklightDimmer d;
  TEST_ASSERT_EQUAL_UINT8(16, d.level());  // driver power-up level
  TEST_ASSERT_EQUAL_UINT8(16, d.update(0, PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(16, d.update(BacklightDimmer::kStepMs - 1, PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(15, d.update(BacklightDimmer::kStepMs, PetState::Idle, false));
  uint32_t t = BacklightDimmer::kStepMs;
  for (int i = 0; i < 10; ++i) d.update(t += BacklightDimmer::kStepMs, PetState::Idle, false);
  TEST_ASSERT_EQUAL_UINT8(12, d.level());  // stops at the target
  // Asleep: down to 7, then back up on waking.
  for (int i = 0; i < 4; ++i) d.update(t += BacklightDimmer::kStepMs, PetState::Sleep, false);
  TEST_ASSERT_EQUAL_UINT8(8, d.level());
  d.update(t += BacklightDimmer::kStepMs, PetState::Sleep, false);
  TEST_ASSERT_EQUAL_UINT8(7, d.level());
  d.update(t += BacklightDimmer::kStepMs, PetState::Happy, false);
  TEST_ASSERT_EQUAL_UINT8(8, d.level());
}

void card_jumps_to_full_and_fades_back() {
  BacklightDimmer d;
  uint32_t t = 0;
  for (int i = 0; i < 20; ++i) d.update(t += BacklightDimmer::kStepMs, PetState::Sleep, false);
  TEST_ASSERT_EQUAL_UINT8(7, d.level());
  TEST_ASSERT_EQUAL_UINT8(16, d.update(t + 1, PetState::Sleep, true));
  TEST_ASSERT_EQUAL_UINT8(16, d.update(t + 2, PetState::Sleep, false));  // waits a step
  TEST_ASSERT_EQUAL_UINT8(15, d.update(t + 1 + BacklightDimmer::kStepMs, PetState::Sleep, false));
}

void survives_millis_wrap() {
  BacklightDimmer d;
  TEST_ASSERT_EQUAL_UINT8(15, d.update(UINT32_MAX - 10, PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(15, d.update(UINT32_MAX, PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(15, d.update(BacklightDimmer::kStepMs - 12, PetState::Idle, false));
  TEST_ASSERT_EQUAL_UINT8(14, d.update(BacklightDimmer::kStepMs - 11, PetState::Idle, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(target_by_state_and_card);
  RUN_TEST(levels_are_clamped_to_the_driver_range);
  RUN_TEST(fades_one_step_per_period);
  RUN_TEST(card_jumps_to_full_and_fades_back);
  RUN_TEST(survives_millis_wrap);
  return UNITY_END();
}
