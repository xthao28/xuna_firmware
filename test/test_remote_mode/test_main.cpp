#include <unity.h>

#include <xuna/hid/RemoteMode.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>
#include <xuna/visual/SymbioticRoom.h>

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

void remote_stop_status_notification_encodes_kind_3_and_event_arg_1() {
  uint8_t b[16] = {};
  xuna::V1LifeCoreAdapter::encodeStatus(3, 0, 0, 1, 42, 7000, b);
  TEST_ASSERT_EQUAL_UINT8(1, b[0]);  // version
  TEST_ASSERT_EQUAL_UINT8(3, b[1]);  // kind = remoteEvent (UDC-7 S7.3)
  TEST_ASSERT_EQUAL_UINT8(0, b[6]);  // result = ok
  TEST_ASSERT_EQUAL_UINT8(1, b[7]);  // event arg = 1 (stop)
}

void remote_workflows_layout_triggers_slots_1_and_2_and_opcode_13_arg_4() {
  RemoteMode remote;
  remote.setLayout(RemoteLayout::Workflows);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RemoteLayout::Workflows), uint8_t(remote.layout()));

  RemoteInput enterEv;
  enterEv.idleHold = true;
  remote.update(enterEv, true, 1000, false);
  TEST_ASSERT_TRUE(remote.active());

  // GPIO0 short -> workflow slot 1 (no ConsumerKey).
  RemoteInput slot1Ev;
  slot1Ev.happy = true;
  const auto out1 = remote.update(slot1Ev, true, 2000, false);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::None), uint8_t(out1.key));
  TEST_ASSERT_EQUAL_UINT8(1, out1.workflowSlot);
  TEST_ASSERT_TRUE(out1.consumed);

  // GPIO14 short -> workflow slot 2 (no ConsumerKey).
  RemoteInput slot2Ev;
  slot2Ev.idle = true;
  const auto out2 = remote.update(slot2Ev, true, 3000, false);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::None), uint8_t(out2.key));
  TEST_ASSERT_EQUAL_UINT8(2, out2.workflowSlot);
  TEST_ASSERT_TRUE(out2.consumed);

  // Opcode 13 arg=4 source=0|1 valid; source=2 invalid.
  const uint8_t cmdMedia[8] = {1, 13, 10, 0, 0, 0, 4, 0};
  const uint8_t cmdWorkflows[8] = {1, 13, 11, 0, 0, 0, 4, 1};
  const uint8_t cmdInvalid[8] = {1, 13, 12, 0, 0, 0, 4, 2};
  const auto parsedMedia = xuna::parseCommand(cmdMedia, 8, false, false, false, false, false, false, true);
  const auto parsedWf = xuna::parseCommand(cmdWorkflows, 8, false, false, false, false, false, false, true);
  const auto parsedBad = xuna::parseCommand(cmdInvalid, 8, false, false, false, false, false, false, true);
  TEST_ASSERT_EQUAL_UINT8(0, parsedMedia.error);
  TEST_ASSERT_EQUAL_UINT8(4, parsedMedia.arg);
  TEST_ASSERT_EQUAL_UINT8(0, parsedMedia.source);
  TEST_ASSERT_EQUAL_UINT8(0, parsedWf.error);
  TEST_ASSERT_EQUAL_UINT8(4, parsedWf.arg);
  TEST_ASSERT_EQUAL_UINT8(1, parsedWf.source);
  TEST_ASSERT_EQUAL_UINT8(4, parsedBad.error);

  // Status kind 3 arg=2 (slot 1) and arg=3 (slot 2).
  uint8_t b1[16] = {};
  uint8_t b2[16] = {};
  xuna::V1LifeCoreAdapter::encodeStatus(3, 0, 0, 2, 43, 8000, b1);
  xuna::V1LifeCoreAdapter::encodeStatus(3, 0, 0, 3, 44, 9000, b2);
  TEST_ASSERT_EQUAL_UINT8(3, b1[1]);
  TEST_ASSERT_EQUAL_UINT8(2, b1[7]);
  TEST_ASSERT_EQUAL_UINT8(3, b2[1]);
  TEST_ASSERT_EQUAL_UINT8(3, b2[7]);
}

void action_approval_confirm_handles_approve_reject_cancel_timeout_and_opcode_13() {
  ActionApprovalConfirm confirm;
  TEST_ASSERT_FALSE(confirm.pending());

  // Start with default 0 -> 30 s timeout.
  confirm.start(0, 1000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_EQUAL_UINT32(30000, confirm.timeoutMs());

  // GPIO0 short -> ApprovalApprove (4).
  RemoteInput approveEv;
  approveEv.happy = true;
  const auto resApprove = confirm.update(approveEv, 2500);
  TEST_ASSERT_FALSE(confirm.pending());
  TEST_ASSERT_TRUE(resApprove.consumed);
  TEST_ASSERT_FALSE(resApprove.expired);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RemoteEventCode::ApprovalApprove), uint8_t(resApprove.decision));

  // Start with explicit 20 s timeout -> GPIO14 short -> ApprovalReject (5).
  confirm.start(20, 3000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_EQUAL_UINT32(20000, confirm.timeoutMs());
  RemoteInput rejectEv;
  rejectEv.idle = true;
  const auto resReject = confirm.update(rejectEv, 4000);
  TEST_ASSERT_FALSE(confirm.pending());
  TEST_ASSERT_TRUE(resReject.consumed);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RemoteEventCode::ApprovalReject), uint8_t(resReject.decision));

  // Cancel clears pending state.
  confirm.start(15, 5000);
  TEST_ASSERT_TRUE(confirm.pending());
  confirm.cancel();
  TEST_ASSERT_FALSE(confirm.pending());

  // Timeout after 10 s expires and clears pending state.
  confirm.start(10, 6000);
  const RemoteInput none;
  const auto beforeTimeout = confirm.update(none, 6000 + 9999);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_FALSE(beforeTimeout.expired);
  const auto atTimeout = confirm.update(none, 6000 + 10000);
  TEST_ASSERT_FALSE(confirm.pending());
  TEST_ASSERT_TRUE(atTimeout.expired);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RemoteEventCode::None), uint8_t(atTimeout.decision));

  // Opcode 13 arg=5 (PromptApproval, source <= 120) and arg=6 (CancelApproval, source == 0).
  const uint8_t cmdPrompt[8] = {1, 13, 20, 0, 0, 0, 5, 30};
  const uint8_t cmdPromptBad[8] = {1, 13, 21, 0, 0, 0, 5, 121};
  const uint8_t cmdCancel[8] = {1, 13, 22, 0, 0, 0, 6, 0};
  const uint8_t cmdCancelBad[8] = {1, 13, 23, 0, 0, 0, 6, 1};
  const auto pPrompt = xuna::parseCommand(cmdPrompt, 8, false, false, false, false, false, false, true);
  const auto pPromptBad = xuna::parseCommand(cmdPromptBad, 8, false, false, false, false, false, false, true);
  const auto pCancel = xuna::parseCommand(cmdCancel, 8, false, false, false, false, false, false, true);
  const auto pCancelBad = xuna::parseCommand(cmdCancelBad, 8, false, false, false, false, false, false, true);
  TEST_ASSERT_EQUAL_UINT8(0, pPrompt.error);
  TEST_ASSERT_EQUAL_UINT8(5, pPrompt.arg);
  TEST_ASSERT_EQUAL_UINT8(30, pPrompt.source);
  TEST_ASSERT_EQUAL_UINT8(4, pPromptBad.error);
  TEST_ASSERT_EQUAL_UINT8(0, pCancel.error);
  TEST_ASSERT_EQUAL_UINT8(6, pCancel.arg);
  TEST_ASSERT_EQUAL_UINT8(4, pCancelBad.error);

  // Status kind 3 arg=4 (approve) and arg=5 (reject).
  uint8_t bApprove[16] = {};
  uint8_t bReject[16] = {};
  xuna::V1LifeCoreAdapter::encodeStatus(3, 0, 0, 4, 45, 10000, bApprove);
  xuna::V1LifeCoreAdapter::encodeStatus(3, 0, 0, 5, 46, 11000, bReject);
  TEST_ASSERT_EQUAL_UINT8(3, bApprove[1]);
  TEST_ASSERT_EQUAL_UINT8(4, bApprove[7]);
  TEST_ASSERT_EQUAL_UINT8(3, bReject[1]);
  TEST_ASSERT_EQUAL_UINT8(5, bReject[7]);
}

void sym3_epigenetic_palette_room_props_and_rhythm_tricks_work() {
  using namespace xuna;
  const uint8_t petA[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const EpigeneticTint tintA = EpigeneticPalette::tintForPet(petA, 0);
  TEST_ASSERT_TRUE(uint8_t(tintA) >= 1 && uint8_t(tintA) <= 5);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(EpigeneticTint::Original),
                          uint8_t(EpigeneticPalette::tintForPet(nullptr, 0)));

  // Dark outline (0x0000) and near-white highlight (0xFFFF) stay untouched.
  TEST_ASSERT_EQUAL_HEX16(0x0000, EpigeneticPalette::applyRgb565(0x0000, EpigeneticTint::CyanMint));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, EpigeneticPalette::applyRgb565(0xFFFF, EpigeneticTint::WarmCoral));
  // Mid-tone body pixel shifts deterministically.
  const uint16_t midPixel = uint16_t((16u << 11) | (32u << 5) | 16u);
  TEST_ASSERT_NOT_EQUAL(midPixel, EpigeneticPalette::applyRgb565(midPixel, EpigeneticTint::CyanMint));

  // Symbiotic Room micro-props return non-zero bitmasks.
  TEST_ASSERT_NOT_EQUAL(0, SymbioticRoomProps::bitmaskRow(RoomPropKind::FocusLamp, 2));
  TEST_ASSERT_NOT_EQUAL(0, SymbioticRoomProps::bitmaskRow(RoomPropKind::DreamBubble, 1));
  TEST_ASSERT_NOT_EQUAL(0, SymbioticRoomProps::bitmaskRow(RoomPropKind::WatchdogBeacon, 5));

  // 2-Button Rhythm Trick Decoder: A-B-A -> SpinDance, B-A-B -> CuriousPeek, A-A-B -> CozyNuzzle.
  RhythmTrickDecoder decoder;
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(true, 1000)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(false, 1300)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::SpinDance), uint8_t(decoder.recordShortPress(true, 1600)));

  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(false, 3000)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(true, 3300)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::CuriousPeek), uint8_t(decoder.recordShortPress(false, 3600)));

  // Gap > 750 ms resets the partial sequence.
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(true, 5000)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(true, 6000)));  // reset!
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::None), uint8_t(decoder.recordShortPress(true, 6300)));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(RhythmTrickId::CozyNuzzle), uint8_t(decoder.recordShortPress(false, 6600)));

  // L-D3 TrickMasteryTracker masters a trick after 3 repetitions and grows FSRS stability.
  TrickMasteryTracker tracker;
  TEST_ASSERT_FALSE(tracker.mastered(RhythmTrickId::SpinDance));
  tracker.recordPractice(RhythmTrickId::SpinDance, 1000);
  tracker.recordPractice(RhythmTrickId::SpinDance, 2000);
  TEST_ASSERT_FALSE(tracker.mastered(RhythmTrickId::SpinDance));
  tracker.recordPractice(RhythmTrickId::SpinDance, 3000);
  TEST_ASSERT_TRUE(tracker.mastered(RhythmTrickId::SpinDance));
  TEST_ASSERT_EQUAL_UINT8(1, tracker.masteredCount());
  TEST_ASSERT_EQUAL_UINT16(3, tracker.totalReps());
  TEST_ASSERT_TRUE(tracker.stabilityDaysQ4(RhythmTrickId::SpinDance) > 16);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(idle_hold_without_hid_host_is_not_consumed);
  RUN_TEST(in_egg_blocks_remote_entry);
  RUN_TEST(remote_mode_maps_buttons_and_exits_with_scene_advance_or_stop);
  RUN_TEST(remote_mode_exits_on_60s_inactivity_and_on_hid_disconnect);
  RUN_TEST(remote_stop_status_notification_encodes_kind_3_and_event_arg_1);
  RUN_TEST(remote_workflows_layout_triggers_slots_1_and_2_and_opcode_13_arg_4);
  RUN_TEST(action_approval_confirm_handles_approve_reject_cancel_timeout_and_opcode_13);
  RUN_TEST(sym3_epigenetic_palette_room_props_and_rhythm_tricks_work);
  return UNITY_END();
}

