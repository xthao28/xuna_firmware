#include <unity.h>

#include <xuna/lab/BoardLab.h>
#include <xuna/visual/VariantPlayer.h>

#include "../fixtures/v1_snapshots.h"

using namespace xuna;

// Track A (2026-10-24): variants are expression only. LifeCore picks one from
// drives / arrival / rest transitions; VariantPlayer plays it over the pose.

namespace {

const uint32_t kMinute = 60000;

// A core whose owner taught one quiet action in every day part.
LifeCore taught(uint8_t liked) {
  LifeSnapshotV2 s;
  for (uint8_t d = 0; d < LearnerV3Config::kMaxDayParts; ++d)
    for (uint8_t a = 0; a < 3; ++a) s.learner.state(0, d, a, a == liked);
  LifeCore core;
  core.restore(s);
  core.seedPersonality(0x5eed1234U);
  core.begin(0, true);
  return core;
}

// XUNA alone for `minutes`, then one spontaneous choice.
LifeCoreStepOutput aloneThenChoose(LifeCore& core, uint32_t& now, uint32_t minutes) {
  for (uint32_t m = 0; m < minutes; ++m) core.step(LifeCoreStepInput(now += kMinute));
  const LifeCoreEvent own(LifeCoreEventKind::Spontaneous);
  return core.step(LifeCoreStepInput(now += 1000, &own, 1));
}

}  // namespace

void player_plays_loops_twice_one_shots_once() {
  VariantPlayer p;
  TEST_ASSERT_EQUAL_INT(-1, p.frame(0, PetState::Idle, 100));
  p.start(VisualVariant::YawnStretch, 1000);
  TEST_ASSERT_EQUAL_INT(128 + 16, p.frame(1000, PetState::Sleep, 100));   // row 1
  TEST_ASSERT_EQUAL_INT(128 + 16 + 15, p.frame(2599, PetState::Sleep, 100));
  TEST_ASSERT_EQUAL_INT(-1, p.frame(2600, PetState::Sleep, 100));         // one cycle
  p.start(VisualVariant::LookAround, 0);
  TEST_ASSERT_EQUAL_INT(128, p.frame(0, PetState::Idle, 85));             // row 0, tempo 85 ms
  TEST_ASSERT_EQUAL_INT(128 + 1, p.frame(1360 + 85, PetState::Idle, 85)); // second cycle
  TEST_ASSERT_EQUAL_INT(-1, p.frame(32 * 85, PetState::Idle, 85));        // two cycles
  p.start(VisualVariant::GreetNuzzle, 0);
  TEST_ASSERT_EQUAL_INT(128 + 48, p.frame(0, PetState::Happy, 100));      // row 3
  TEST_ASSERT_EQUAL_INT(-1, p.frame(300, PetState::Idle, 100));           // pose changed: cancelled
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::None), uint8_t(p.playing()));
  p.start(VisualVariant::None, 0);
  TEST_ASSERT_EQUAL_INT(-1, p.frame(0, PetState::Idle, 100));
}

void owner_arrival_greets_once_per_session() {
  BoardLab lab(7);
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  TEST_ASSERT_TRUE(lab.boot(0));
  TEST_ASSERT_TRUE(lab.greeting(1000));
  LifeCore& core = const_cast<LifeCore&>(lab.core());
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::GreetNuzzle), uint8_t(core.takeVariant()));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::None), uint8_t(core.takeVariant()));  // taken
  lab.idleButton(20000);  // same session: no second greeting
  TEST_ASSERT_TRUE(core.takeVariant() != VisualVariant::GreetNuzzle);
  // Back after 30 minutes away: greeted again.
  uint32_t now = 20000;
  while (now < 20000 + 31 * kMinute) lab.loop(now += 1000);
  core.takeVariant();
  lab.greeting(now += 1000);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::GreetNuzzle), uint8_t(core.takeVariant()));
}

void explore_drive_turns_an_idle_choice_into_looking_around() {
  LifeCore core = taught(0);  // idle
  uint32_t now = 0;
  LifeCoreStepOutput out = aloneThenChoose(core, now, 5);  // explore still low
  TEST_ASSERT_TRUE(out.decisionShown);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ActionId::Idle), out.selectedAction);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::None), uint8_t(out.variant));
  out = aloneThenChoose(core, now, 70);  // ≥ 64 min: explore past half
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ActionId::Idle), out.selectedAction);  // the choice is the learner's
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::LookAround), uint8_t(out.variant));
  TEST_ASSERT_TRUE(core.personality().drives().state().explore < 32768);  // looking satisfied it
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::LookAround), uint8_t(core.takeVariant()));
}

void a_happy_choice_bounces_when_xuna_is_joyful_or_playful() {
  LifeCore core = taught(1);  // happy
  uint32_t now = 0;
  LifeCoreStepOutput out = aloneThenChoose(core, now, 2);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ActionId::Happy), out.selectedAction);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::None), uint8_t(out.variant));  // calm: plain happy
  // The owner pets it (joy), and XUNA soon picks happy again: it bounces.
  const LifeCoreEvent pet(LifeCoreEventKind::Reaction, 0, 0, 0, true);
  TEST_ASSERT_TRUE(core.step(LifeCoreStepInput(now += 1000, &pet, 1)).reactionApplied);
  TEST_ASSERT_TRUE(core.personality().affect().state().emotionValence >= 4096);
  for (int i = 0; i < 4; ++i) core.step(LifeCoreStepInput(now += 1000));
  const LifeCoreEvent own(LifeCoreEventKind::Spontaneous);
  out = core.step(LifeCoreStepInput(now += 1000, &own, 1));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ActionId::Happy), out.selectedAction);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(VisualVariant::BouncePlay), uint8_t(out.variant));
}

void resting_and_waking_yawn() {
  BoardLab lab(7);
  lab.store().seedV1(xuna_fixture::fresh, sizeof(xuna_fixture::fresh));
  TEST_ASSERT_TRUE(lab.boot(0));
  LifeCore& core = const_cast<LifeCore&>(lab.core());
  uint32_t yawns = 0, rests = 0, wakes = 0;
  Activity last = lab.adapter().last().activity;
  for (uint32_t now = 1000; now < 12UL * 60 * kMinute; now += 1000) {
    lab.loop(now);
    const Activity a = lab.adapter().last().activity;
    const bool restNow = a == Activity::Resting || a == Activity::Sleeping;
    const bool restBefore = last == Activity::Resting || last == Activity::Sleeping;
    if (restNow && !restBefore) ++rests;
    if (!restNow && restBefore) ++wakes;
    if (core.takeVariant() == VisualVariant::YawnStretch) ++yawns;
    last = a;
  }
  TEST_ASSERT_TRUE(rests > 0);  // XUNA rests on its own within 12 hours
  TEST_ASSERT_EQUAL_UINT32(rests + wakes, yawns);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(player_plays_loops_twice_one_shots_once);
  RUN_TEST(owner_arrival_greets_once_per_session);
  RUN_TEST(explore_drive_turns_an_idle_choice_into_looking_around);
  RUN_TEST(a_happy_choice_bounces_when_xuna_is_joyful_or_playful);
  RUN_TEST(resting_and_waking_yawn);
  return UNITY_END();
}
