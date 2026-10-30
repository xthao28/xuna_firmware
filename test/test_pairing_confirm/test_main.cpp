#include <unity.h>

#include <xuna/hid/PairingConfirm.h>
#include <xuna/hid/RemoteMode.h>

using namespace xuna::hid;

void accept_and_reject_short_presses_resolve_pending_prompt() {
  PairingConfirm confirm;
  TEST_ASSERT_FALSE(confirm.pending());

  const auto req1 = confirm.request(123456, 2, 1000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_TRUE(req1.show);
  TEST_ASSERT_EQUAL_UINT32(123456, req1.passkey);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::None), uint8_t(req1.reply));

  RemoteInput acceptEv;
  acceptEv.happy = true;
  const auto acc = confirm.update(acceptEv, 1500);
  TEST_ASSERT_FALSE(confirm.pending());
  TEST_ASSERT_TRUE(acc.consumed);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::Accept), uint8_t(acc.reply));
  TEST_ASSERT_EQUAL_UINT8(2, acc.replyToken);

  confirm.request(654321, 3, 2000);
  RemoteInput rejectEv;
  rejectEv.idle = true;
  const auto rej = confirm.update(rejectEv, 2500);
  TEST_ASSERT_FALSE(confirm.pending());
  TEST_ASSERT_TRUE(rej.consumed);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::Reject), uint8_t(rej.reply));
  TEST_ASSERT_EQUAL_UINT8(3, rej.replyToken);
}

void holds_are_ignored_but_consumed_and_30s_timeout_rejects() {
  PairingConfirm confirm;
  confirm.request(111222, 1, 1000);

  RemoteInput hold1;
  hold1.happyHold = true;
  const auto h1 = confirm.update(hold1, 2000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_TRUE(h1.consumed);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::None), uint8_t(h1.reply));

  RemoteInput hold2;
  hold2.idleHold = true;
  const auto h2 = confirm.update(hold2, 3000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_TRUE(h2.consumed);

  RemoteInput hold3;
  hold3.bothHold = true;
  const auto h3 = confirm.update(hold3, 4000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_TRUE(h3.consumed);

  const RemoteInput none;
  const auto beforeTimeout = confirm.update(none, 1000 + PairingConfirm::kTimeoutMs - 1);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_FALSE(beforeTimeout.consumed);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::None), uint8_t(beforeTimeout.reply));

  const auto timedOut = confirm.update(none, 1000 + PairingConfirm::kTimeoutMs);
  TEST_ASSERT_FALSE(confirm.pending());
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::Reject), uint8_t(timedOut.reply));
  TEST_ASSERT_EQUAL_UINT8(1, timedOut.replyToken);
}

void replacement_request_rejects_old_token_and_shows_new_passkey() {
  PairingConfirm confirm;
  confirm.request(100200, 0, 1000);

  const auto replaced = confirm.request(300400, 1, 5000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_TRUE(replaced.show);
  TEST_ASSERT_EQUAL_UINT32(300400, replaced.passkey);
  TEST_ASSERT_EQUAL_UINT8(uint8_t(PairingReply::Reject), uint8_t(replaced.reply));
  TEST_ASSERT_EQUAL_UINT8(0, replaced.replyToken);
  TEST_ASSERT_EQUAL_UINT8(1, confirm.token());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(accept_and_reject_short_presses_resolve_pending_prompt);
  RUN_TEST(holds_are_ignored_but_consumed_and_30s_timeout_rejects);
  RUN_TEST(replacement_request_rejects_old_token_and_shows_new_passkey);
  return UNITY_END();
}

