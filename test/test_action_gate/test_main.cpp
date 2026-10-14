#include <unity.h>
#include <xuna/life/ActionGate.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/Needs.h>

using namespace xuna;

namespace {
const uint8_t kIdle = static_cast<uint8_t>(ActionId::Idle);
const uint8_t kHappy = static_cast<uint8_t>(ActionId::Happy);
const uint8_t kSleep = static_cast<uint8_t>(ActionId::Sleep);
const uint8_t kCurious = static_cast<uint8_t>(ActionId::Curious);

void boot(ActivityFsm& fsm) {
  ActivityGuards g;
  g.restoreOk = true;
  TEST_ASSERT_TRUE(fsm.request(Activity::Idle, g, 0).ok);
}

// Mirrors the main-loop order: needs update (with the activity that held
// since the previous loop), then the FSM tick.
void loopTick(ActivityFsm& fsm, Needs& needs, MonoMs now) {
  needs.update(now, ActivityFsm::recoversEnergy(fsm.state()));
  ActivityGuards g;
  g.energy = needs.state().energy;
  g.restoreOk = true;
  fsm.tick(now, g);
}
}  // namespace

void accepts_and_maps_visual() {
  ActivityFsm fsm;
  boot(fsm);
  ActionGate gate;
  const ActionOutcome happy = gate.request(kHappy, 0, fsm, 65535, 10);
  TEST_ASSERT_TRUE(happy.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::Accepted, happy.reason);
  TEST_ASSERT_EQUAL(VisualIntent::Happy, happy.visual);
  TEST_ASSERT_EQUAL(Activity::Reacting, fsm.state());
  // Reacting -> Reacting restarts the reaction with a different action.
  const ActionOutcome curious = gate.request(kCurious, 0, fsm, 65535, 20);
  TEST_ASSERT_TRUE(curious.accepted);
  TEST_ASSERT_EQUAL(VisualIntent::Curious, curious.visual);
  TEST_ASSERT_EQUAL_UINT32(20, fsm.enteredAt());
  // Idle while Idle is an accepted no-op.
  TEST_ASSERT_TRUE(gate.request(kIdle, 0, fsm, 65535, 30).accepted);
  TEST_ASSERT_TRUE(gate.request(kIdle, 0, fsm, 65535, 31).accepted);
  TEST_ASSERT_EQUAL(Activity::Idle, fsm.state());
}

void rejects_unknown_parameter_cooldown_and_energy() {
  ActivityFsm fsm;
  boot(fsm);
  ActionGate gate;
  ActionOutcome outcome = gate.request(99, 0, fsm, 65535, 10);
  TEST_ASSERT_FALSE(outcome.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::UnknownAction, outcome.reason);
  outcome = gate.request(kHappy, 1, fsm, 65535, 10);
  TEST_ASSERT_FALSE(outcome.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::InvalidParameter, outcome.reason);
  outcome = gate.request(kHappy, 0, fsm, 4095, 10);
  TEST_ASSERT_FALSE(outcome.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::Energy, outcome.reason);
  TEST_ASSERT_EQUAL(Activity::Idle, fsm.state());

  TEST_ASSERT_TRUE(gate.request(kHappy, 0, fsm, 65535, 100).accepted);
  outcome = gate.request(kHappy, 0, fsm, 65535, 1599);
  TEST_ASSERT_FALSE(outcome.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::Cooldown, outcome.reason);
  TEST_ASSERT_TRUE(gate.request(kHappy, 0, fsm, 65535, 1600).accepted);

  // Cooldown survives a uint32 wrap and is cleared by reset().
  TEST_ASSERT_TRUE(gate.request(kCurious, 0, fsm, 65535, UINT32_MAX - 100).accepted);
  outcome = gate.request(kCurious, 0, fsm, 65535, 100);
  TEST_ASSERT_EQUAL(ValidationReason::Cooldown, outcome.reason);
  gate.reset();
  TEST_ASSERT_TRUE(gate.request(kCurious, 0, fsm, 65535, 101).accepted);
}

void sleeping_wakes_for_non_sleep_actions() {
  ActivityFsm fsm;
  boot(fsm);
  ActionGate gate;
  TEST_ASSERT_TRUE(gate.request(kSleep, 0, fsm, 65535, 10).accepted);
  TEST_ASSERT_EQUAL(Activity::Sleeping, fsm.state());
  TEST_ASSERT_TRUE(gate.request(kSleep, 0, fsm, 65535, 6000).accepted);
  TEST_ASSERT_EQUAL(Activity::Sleeping, fsm.state());

  // Not enough energy: stay asleep instead of waking for nothing.
  ActionOutcome outcome = gate.request(kHappy, 0, fsm, 100, 7000);
  TEST_ASSERT_FALSE(outcome.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::Energy, outcome.reason);
  TEST_ASSERT_EQUAL(Activity::Sleeping, fsm.state());

  outcome = gate.request(kHappy, 0, fsm, 65535, 7001);
  TEST_ASSERT_TRUE(outcome.accepted);
  TEST_ASSERT_EQUAL(Activity::Reacting, fsm.state());

  TEST_ASSERT_TRUE(gate.request(kSleep, 0, fsm, 65535, 12000).accepted);
  TEST_ASSERT_TRUE(gate.request(kIdle, 0, fsm, 65535, 12001).accepted);
  TEST_ASSERT_EQUAL(Activity::Idle, fsm.state());
}

void sleep_routes_through_idle_unless_guarded() {
  ActivityFsm fsm;
  boot(fsm);
  ActionGate gate;
  TEST_ASSERT_TRUE(gate.request(kHappy, 0, fsm, 65535, 10).accepted);
  TEST_ASSERT_EQUAL(Activity::Reacting, fsm.state());
  TEST_ASSERT_TRUE(gate.request(kSleep, 0, fsm, 65535, 20).accepted);
  TEST_ASSERT_EQUAL(Activity::Sleeping, fsm.state());

  // Resting -> Idle needs kRestExit energy; the validator passes but the FSM
  // guard refuses, and nothing changes (adapter reports busy).
  ActivityFsm resting;
  boot(resting);
  ActivityGuards low;
  low.energy = ActivityFsm::kRestThreshold - 1;
  TEST_ASSERT_TRUE(resting.request(Activity::Resting, low, 30).ok);
  ActionGate fresh;
  const ActionOutcome outcome =
      fresh.request(kSleep, 0, resting, ActivityFsm::kRestThreshold - 1, 40);
  TEST_ASSERT_FALSE(outcome.accepted);
  TEST_ASSERT_EQUAL(ValidationReason::Accepted, outcome.reason);
  TEST_ASSERT_EQUAL(Activity::Resting, resting.state());
  // The refused request must not start a cooldown.
  TEST_ASSERT_TRUE(fresh.request(kSleep, 0, resting, ActivityFsm::kRestExit, 41).accepted);
  TEST_ASSERT_EQUAL(Activity::Sleeping, resting.state());
}

void error_safe_rejects_every_action() {
  ActivityFsm fsm;
  ActivityGuards g;
  TEST_ASSERT_TRUE(fsm.request(Activity::ErrorSafe, g, 0).ok);
  ActionGate gate;
  for (uint8_t id = 0; id < 4; ++id) {
    TEST_ASSERT_FALSE(gate.request(id, 0, fsm, 65535, 10 + id).accepted);
    TEST_ASSERT_EQUAL(Activity::ErrorSafe, fsm.state());
  }
}

// Regression: energy used to decay without any recovery, so after about 48
// minutes the pet stayed in Resting forever and later rejected every action.
void long_run_recovers_from_resting() {
  ActivityFsm fsm;
  boot(fsm);
  Needs needs;
  needs.reset(0);
  ActionGate gate;
  bool sawResting = false, sawRestExit = false;
  uint32_t happyAccepted = 0, happyAttempts = 0;
  Activity previous = fsm.state();
  for (MonoMs now = 100; now <= 3U * 60U * 60000U; now += 100) {
    loopTick(fsm, needs, now);
    if (fsm.state() == Activity::Resting) sawResting = true;
    if (previous == Activity::Resting && fsm.state() == Activity::Idle)
      sawRestExit = true;
    previous = fsm.state();
    if (now % 60000U == 0 && fsm.state() == Activity::Idle) {
      ++happyAttempts;
      if (gate.request(kHappy, 0, fsm, needs.state().energy, now).accepted)
        ++happyAccepted;
    }
  }
  TEST_ASSERT_TRUE(sawResting);
  TEST_ASSERT_TRUE(sawRestExit);
  TEST_ASSERT_TRUE(happyAttempts > 60);
  TEST_ASSERT_EQUAL_UINT32(happyAttempts, happyAccepted);
  // The tired cycle stays above the auto-sleep threshold while awake/resting.
  TEST_ASSERT_TRUE(needs.state().energy >= ActivityFsm::kSleepThreshold);
  TEST_ASSERT_TRUE(fsm.state() != Activity::Sleeping);
}

void sleeping_recovers_to_full() {
  ActivityFsm fsm;
  boot(fsm);
  Needs needs;
  needs.reset(0);
  ActionGate gate;
  for (MonoMs now = 100; now <= 30U * 60000U; now += 100)
    loopTick(fsm, needs, now);
  const uint16_t before = needs.state().energy;
  TEST_ASSERT_TRUE(gate.request(kSleep, 0, fsm, before, 30U * 60000U).accepted);
  for (MonoMs now = 30U * 60000U + 100; now <= 90U * 60000U; now += 100)
    loopTick(fsm, needs, now);
  TEST_ASSERT_EQUAL(Activity::Sleeping, fsm.state());
  TEST_ASSERT_EQUAL_UINT16(65535, needs.state().energy);
}

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(accepts_and_maps_visual);
  RUN_TEST(rejects_unknown_parameter_cooldown_and_energy);
  RUN_TEST(sleeping_wakes_for_non_sleep_actions);
  RUN_TEST(sleep_routes_through_idle_unless_guarded);
  RUN_TEST(error_safe_rejects_every_action);
  RUN_TEST(long_run_recovers_from_resting);
  RUN_TEST(sleeping_recovers_to_full);
  return UNITY_END();
}
