#include <unity.h>
#include <xuna/life/ActivityFsm.h>

using namespace xuna;

namespace {
ActivityGuards ready() {
  ActivityGuards g;
  g.restoreOk = true;
  g.explicitWake = true;
  g.evolutionComplete = true;
  g.evolutionReady = true;
  g.reboot = true;
  g.stage = 1;
  g.energy = ActivityFsm::kPlayMin;
  return g;
}

bool expectedLegal(Activity from, Activity to) {
  switch (from) {
    case Activity::Booting:
      return to == Activity::Idle || to == Activity::ErrorSafe;
    case Activity::Idle:
      return to == Activity::Observing || to == Activity::Reacting ||
             to == Activity::Playing || to == Activity::Resting ||
             to == Activity::Sleeping || to == Activity::EvolutionTransition;
    case Activity::Observing:
      return to == Activity::Idle || to == Activity::Reacting;
    case Activity::Reacting:
      return to == Activity::Idle || to == Activity::Reacting;
    case Activity::Playing:
      return to == Activity::Idle || to == Activity::Reacting;
    case Activity::Resting:
      return to == Activity::Idle || to == Activity::Reacting;
    case Activity::Sleeping:
      return to == Activity::Idle;
    case Activity::EvolutionTransition:
      return to == Activity::Idle;
    case Activity::ErrorSafe:
      return to == Activity::Booting;
  }
  return false;
}

// Starts a fresh FSM and drives it to `state` through legal transitions only.
void enter(ActivityFsm& fsm, Activity state, MonoMs now = 100) {
  fsm = ActivityFsm();
  ActivityGuards g = ready();
  if (state == Activity::Booting) return;
  if (state == Activity::ErrorSafe) {
    g.restoreOk = false;
    TEST_ASSERT_TRUE(fsm.request(Activity::ErrorSafe, g, now).ok);
    return;
  }
  TEST_ASSERT_TRUE(fsm.request(Activity::Idle, g, now).ok);
  if (state == Activity::Idle) return;
  if (state == Activity::Resting) g.energy = ActivityFsm::kRestThreshold - 1;
  TEST_ASSERT_TRUE(fsm.request(state, g, now).ok);
  TEST_ASSERT_EQUAL(state, fsm.state());
}
}  // namespace

void exhaustive_pairs() {
  for (uint8_t from = 0; from < activityCount(); ++from) {
    for (uint8_t to = 0; to < activityCount(); ++to) {
      ActivityFsm fsm;
      ActivityGuards g = ready();
      enter(fsm, static_cast<Activity>(from));
      const TransitionResult result = fsm.request(static_cast<Activity>(to), g, 200);
      const bool legal =
          expectedLegal(static_cast<Activity>(from), static_cast<Activity>(to));
      TEST_ASSERT_EQUAL(legal, ActivityFsm::legal(static_cast<Activity>(from),
                                                  static_cast<Activity>(to)));
      if (!legal) {
        TEST_ASSERT_FALSE(result.ok);
        TEST_ASSERT_EQUAL(TransitionReason::Illegal, result.reason);
      } else {
        TEST_ASSERT_NOT_EQUAL(TransitionReason::Illegal, result.reason);
      }
    }
  }
}

void guards_and_reasons() {
  ActivityFsm fsm;
  ActivityGuards g = ready();
  g.restoreOk = false;
  TEST_ASSERT_EQUAL(TransitionReason::GuardRestore,
                    fsm.request(Activity::Idle, g, 0).reason);
  g.restoreOk = true;
  TEST_ASSERT_TRUE(fsm.request(Activity::Idle, g, 0).ok);
  g.energy = ActivityFsm::kPlayMin - 1;
  TransitionResult result = fsm.request(Activity::Playing, g, 1);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_EQUAL(TransitionReason::GuardEnergy, result.reason);
  g.energy = ActivityFsm::kPlayMin;
  g.busy = true;
  result = fsm.request(Activity::Reacting, g, 2);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_EQUAL(TransitionReason::Busy, result.reason);
  g.busy = false;
  g.evolutionReady = false;
  result = fsm.request(Activity::EvolutionTransition, g, 3);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_EQUAL(TransitionReason::GuardStage, result.reason);
  g.evolutionReady = true;
  g.stage = 0;
  result = fsm.request(Activity::EvolutionTransition, g, 4);
  TEST_ASSERT_FALSE(result.ok);
  TEST_ASSERT_EQUAL(TransitionReason::GuardStage, result.reason);
}

void timeout_and_wrap() {
  ActivityFsm fsm;
  ActivityGuards g = ready();
  TEST_ASSERT_TRUE(fsm.request(Activity::Idle, g, 0).ok);
  TEST_ASSERT_TRUE(fsm.request(Activity::Reacting, g, UINT32_MAX - 100).ok);
  TEST_ASSERT_EQUAL(Activity::Reacting, fsm.state());
  TEST_ASSERT_FALSE(fsm.tick(100));
  TEST_ASSERT_EQUAL(Activity::Reacting, fsm.state());
  TEST_ASSERT_TRUE(fsm.tick(2300));
  TEST_ASSERT_EQUAL(Activity::Idle, fsm.state());

  ActivityFsm boot;
  TEST_ASSERT_FALSE(boot.tick(ActivityFsm::kBootingMaxDwellMs - 1));
  TEST_ASSERT_EQUAL(Activity::Booting, boot.state());
  TEST_ASSERT_TRUE(boot.tick(ActivityFsm::kBootingMaxDwellMs));
  TEST_ASSERT_EQUAL(Activity::ErrorSafe, boot.state());
  TEST_ASSERT_FALSE(boot.tick(UINT32_MAX));
  TEST_ASSERT_EQUAL(Activity::ErrorSafe, boot.state());
}

void guarded_exits() {
  ActivityFsm fsm;
  ActivityGuards g = ready();
  enter(fsm, Activity::Sleeping);
  g.explicitWake = false;
  TEST_ASSERT_EQUAL(TransitionReason::GuardWake,
                    fsm.request(Activity::Idle, g, 200).reason);
  g.explicitWake = true;
  TEST_ASSERT_TRUE(fsm.request(Activity::Idle, g, 201).ok);

  enter(fsm, Activity::Resting);
  g.energy = ActivityFsm::kRestExit - 1;
  TEST_ASSERT_EQUAL(TransitionReason::GuardEnergy,
                    fsm.request(Activity::Idle, g, 300).reason);
  g.energy = ActivityFsm::kRestExit;
  TEST_ASSERT_TRUE(fsm.request(Activity::Idle, g, 301).ok);

  enter(fsm, Activity::ErrorSafe);
  g.reboot = false;
  TEST_ASSERT_EQUAL(TransitionReason::Illegal,
                    fsm.request(Activity::Idle, g, 400).reason);
  TEST_ASSERT_EQUAL(TransitionReason::GuardReboot,
                    fsm.request(Activity::Booting, g, 400).reason);
  g.reboot = true;
  TEST_ASSERT_TRUE(fsm.request(Activity::Booting, g, 401).ok);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(exhaustive_pairs);
  RUN_TEST(guards_and_reasons);
  RUN_TEST(timeout_and_wrap);
  RUN_TEST(guarded_exits);
  return UNITY_END();
}
