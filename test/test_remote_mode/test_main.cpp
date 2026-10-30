#include <unity.h>

#include <xuna/hid/RemoteMode.h>

using namespace xuna::hid;

void idle_hold_without_hid_host_is_not_consumed() {
  RemoteMode remote;
  RemoteInput ev;
  ev.idleHold = true;
  const auto out = remote.update(ev, false, 1000, false);
  TEST_ASSERT_FALSE(remote.active());
  TEST_ASSERT_FALSE(out.enter);
  TEST_ASSERT_FALSE(out.consumed);
}

void in_egg_blocks_remote_entry() {
  RemoteMode remote;
  RemoteInput ev;
  ev.idleHold = true;
  const auto out = remote.update(ev, true, 1000, true);
  TEST_ASSERT_FALSE(remote.active());
  TEST_ASSERT_FALSE(out.enter);
  TEST_ASSERT_FALSE(out.consumed);
}

void remote_mode_maps_buttons_and_exits_with_scene_advance_or_stop() {
  RemoteMode remote;

  // Enter Remote via GPIO14 hold when HID host is connected.
  RemoteInput enterEv;
  enterEv.idleHold = true;
  const auto entered = remote.update(enterEv, true, 1000, false);
  TEST_ASSERT_TRUE(remote.active());
  TEST_ASSERT_TRUE(entered.enter);
  TEST_ASSERT_FALSE(entered.advanceScene);
  TEST_ASSERT_TRUE(entered.consumed);

  // GPIO0 short -> PlayPause.
  RemoteInput playEv;
  playEv.happy = true;
  const auto play = remote.update(playEv, true, 2000, false);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::PlayPause), uint8_t(play.key));
  TEST_ASSERT_TRUE(play.consumed);

  // GPIO14 short -> Next.
  RemoteInput nextEv;
  nextEv.idle = true;
  const auto next = remote.update(nextEv, true, 3000, false);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::Next), uint8_t(next.key));
  TEST_ASSERT_TRUE(next.consumed);

  // GPIO0 hold -> Previous (no status card).
  RemoteInput prevEv;
  prevEv.happyHold = true;
  const auto prev = remote.update(prevEv, true, 4000, false);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::Previous), uint8_t(prev.key));
  TEST_ASSERT_TRUE(prev.consumed);

  // GPIO14 hold -> exit and advanceScene.
  RemoteInput exitEv;
  exitEv.idleHold = true;
  const auto exited = remote.update(exitEv, true, 5000, false);
  TEST_ASSERT_FALSE(remote.active());
  TEST_ASSERT_TRUE(exited.exit);
  TEST_ASSERT_TRUE(exited.advanceScene);
  TEST_ASSERT_FALSE(exited.stop);
  TEST_ASSERT_TRUE(exited.consumed);

  // Re-enter and exit via bothHold -> stop = true, advanceScene = false.
  remote.update(enterEv, true, 6000, false);
  TEST_ASSERT_TRUE(remote.active());
  RemoteInput stopEv;
  stopEv.bothHold = true;
  const auto stopped = remote.update(stopEv, true, 7000, false);
  TEST_ASSERT_FALSE(remote.active());
  TEST_ASSERT_TRUE(stopped.exit);
  TEST_ASSERT_TRUE(stopped.stop);
  TEST_ASSERT_FALSE(stopped.advanceScene);
  TEST_ASSERT_TRUE(stopped.consumed);
}

void remote_mode_exits_on_60s_inactivity_and_on_hid_disconnect() {
  RemoteMode remote;
  RemoteInput enterEv;
  enterEv.idleHold = true;
  remote.update(enterEv, true, 1000, false);
  TEST_ASSERT_TRUE(remote.active());

  // Button at t=31000 refreshes the 60 s timer.
  RemoteInput playEv;
  playEv.happy = true;
  remote.update(playEv, true, 31000, false);

  const RemoteInput none;
  const auto beforeTimeout = remote.update(none, true, 31000 + RemoteMode::kTimeoutMs - 1, false);
  TEST_ASSERT_FALSE(beforeTimeout.exit);
  TEST_ASSERT_TRUE(remote.active());

  const auto atTimeout = remote.update(none, true, 31000 + RemoteMode::kTimeoutMs, false);
  TEST_ASSERT_TRUE(atTimeout.exit);
  TEST_ASSERT_FALSE(atTimeout.advanceScene);
  TEST_ASSERT_FALSE(remote.active());

  // Re-enter and drop HID connection -> immediate exit.
  remote.update(enterEv, true, 100000, false);
  TEST_ASSERT_TRUE(remote.active());
  const auto lost = remote.update(none, false, 100500, false);
  TEST_ASSERT_TRUE(lost.exit);
  TEST_ASSERT_FALSE(remote.active());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(idle_hold_without_hid_host_is_not_consumed);
  RUN_TEST(in_egg_blocks_remote_entry);
  RUN_TEST(remote_mode_maps_buttons_and_exits_with_scene_advance_or_stop);
  RUN_TEST(remote_mode_exits_on_60s_inactivity_and_on_hid_disconnect);
  return UNITY_END();
}

