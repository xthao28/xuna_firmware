#include <unity.h>
#include <xuna/base/Random.h>
#include <xuna/AnimationManager.h>
#include <xuna/life/ActionRegistry.h>
#include <xuna/life/BehaviorSelector.h>
#include <xuna/life/RequestValidator.h>
#include <xuna/visual/VisualIntent.h>

using namespace xuna;

void action_table() {
  TEST_ASSERT_EQUAL_UINT32(4, kActionDescriptorCount);
  for (size_t i = 0; i < kActionDescriptorCount; ++i) {
    const ActionDescriptor& d = kActionDescriptors[i];
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(d.actionId), i);
    TEST_ASSERT_EQUAL(PermissionTier::Public, d.permission);
    TEST_ASSERT_FALSE(d.requiresBond);
    TEST_ASSERT_NOT_NULL(findAction(static_cast<uint8_t>(d.actionId)));
  }
  TEST_ASSERT_NULL(findAction(99));
  TEST_ASSERT_EQUAL(Activity::Reacting,
                    activityForAction(ActionId::Happy));
  TEST_ASSERT_EQUAL(Activity::Sleeping,
                    activityForAction(ActionId::Sleep));
}

ValidatorContext context() {
  ValidatorContext c;
  c.activity = Activity::Idle;
  c.stage = 0;
  c.energy = 60000;
  c.now = 10000;
  return c;
}

void validator_table() {
  ValidatorContext c = context();
  ValidationResult result =
      RequestValidator::validate({static_cast<uint8_t>(ActionId::Happy), 0}, c);
  TEST_ASSERT_TRUE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::Accepted, result.reason);
  TEST_ASSERT_EQUAL(VisualIntent::Happy, result.visual);

  result = RequestValidator::validate({static_cast<uint8_t>(ActionId::Happy), 1}, c);
  TEST_ASSERT_FALSE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::InvalidParameter, result.reason);
  result = RequestValidator::validate({99, 0}, c);
  TEST_ASSERT_FALSE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::UnknownAction, result.reason);

  c.energy = 1000;
  result = RequestValidator::validate({static_cast<uint8_t>(ActionId::Happy), 0}, c);
  TEST_ASSERT_FALSE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::Energy, result.reason);

  c = context();
  c.acceptedMask = uint8_t(1) << static_cast<uint8_t>(ActionId::Happy);
  c.lastAcceptedAt[static_cast<uint8_t>(ActionId::Happy)] = 9500;
  result = RequestValidator::validate({static_cast<uint8_t>(ActionId::Happy), 0}, c);
  TEST_ASSERT_FALSE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::Cooldown, result.reason);
  c.now = 11000;
  TEST_ASSERT_TRUE(RequestValidator::validate(
                       {static_cast<uint8_t>(ActionId::Happy), 0}, c)
                       .allowed);

  c = context();
  c.busy = true;
  result = RequestValidator::validate({static_cast<uint8_t>(ActionId::Happy), 0}, c);
  TEST_ASSERT_FALSE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::Busy, result.reason);
  c.busy = false;
  c.activity = Activity::Sleeping;
  result = RequestValidator::validate({static_cast<uint8_t>(ActionId::Happy), 0}, c);
  TEST_ASSERT_FALSE(result.allowed);
  TEST_ASSERT_EQUAL(ValidationReason::Activity, result.reason);
}

void visual_fallbacks() {
  for (uint8_t stage = 0; stage < 5; ++stage) {
    for (uint8_t i = 0; i < 8; ++i) {
      const ExpressionResolution r =
          resolveVisualIntent(static_cast<VisualIntent>(i), stage);
      TEST_ASSERT_NOT_EQUAL(VisualIntent::Unknown, r.resolved);
    }
  }
  ExpressionResolution r = resolveVisualIntent(VisualIntent::Celebrate, 0);
  TEST_ASSERT_EQUAL(VisualIntent::Happy, r.resolved);
  TEST_ASSERT_TRUE(r.usedFallback);
  r = resolveVisualIntent(VisualIntent::Greet, 0);
  TEST_ASSERT_EQUAL(VisualIntent::Happy, r.resolved);
  r = resolveVisualIntent(static_cast<VisualIntent>(99), 0);
  TEST_ASSERT_EQUAL(VisualIntent::Idle, r.resolved);
  TEST_ASSERT_TRUE(r.usedFallback);
}

void visual_intent_bridge() {
  AnimationManager animation;
  animation.begin(0);
  animation.setIntent(VisualIntent::Happy, 0, 100);
  TEST_ASSERT_EQUAL(PetState::Happy, animation.state());
  animation.setIntent(VisualIntent::Celebrate, 0, 200);
  TEST_ASSERT_EQUAL(PetState::Happy, animation.state());
  animation.setIntent(VisualIntent::Greet, 0, 300);
  TEST_ASSERT_EQUAL(PetState::Happy, animation.state());
  animation.setIntent(VisualIntent::Sleep, 0, 400);
  TEST_ASSERT_EQUAL(PetState::Sleep, animation.state());
  animation.setIntent(VisualIntent::ErrorSafe, 0, 500);
  TEST_ASSERT_EQUAL(PetState::Sleep, animation.state());
  animation.setIntent(static_cast<VisualIntent>(99), 0, 600);
  TEST_ASSERT_EQUAL(PetState::Idle, animation.state());
}
void selector_determinism_and_filters() {
  const BehaviorCandidate candidates[] = {
      {ActionId::Idle, 10, 0, 0, true, true},
      {ActionId::Happy, 10, 0, 0, true, true},
      {ActionId::Sleep, 100, 0, 0, false, true},
      {ActionId::Curious, 100, 0, 65535, true, true}};
  NeedsState needs;
  needs.energy = 50000;
  MoodState mood;
  mood.arousal = 1000;
  mood.valence = 0;
  Pcg32 first(42, 54), second(42, 54);
  const SelectionResult a =
      BehaviorSelector::choose(candidates, 4, needs, mood, first);
  const SelectionResult b =
      BehaviorSelector::choose(candidates, 4, needs, mood, second);
  TEST_ASSERT_TRUE(a.found);
  TEST_ASSERT_EQUAL(a.action, b.action);
  TEST_ASSERT_EQUAL_INT32(a.score, b.score);
  TEST_ASSERT_TRUE(a.action == ActionId::Idle || a.action == ActionId::Happy);

  const BehaviorCandidate blocked[] = {
      {ActionId::Happy, 1, 0, 0, false, true},
      {ActionId::Curious, 1, 0, 60000, true, false}};
  Pcg32 rng(1, 1);
  TEST_ASSERT_FALSE(BehaviorSelector::choose(blocked, 2, needs, mood, rng).found);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(action_table);
  RUN_TEST(validator_table);
  RUN_TEST(visual_fallbacks);
  RUN_TEST(visual_intent_bridge);
  RUN_TEST(selector_determinism_and_filters);
  return UNITY_END();
}
