#include <unity.h>

#include "xuna/hid/PairingWindow.h"
#include "xuna/hid/RemoteMode.h"

namespace {

using xuna::hid::ForgetAllConfirm;
using xuna::hid::PairingWindow;
using xuna::hid::RemoteInput;

void test_pairing_window_open_conditions_and_expiry() {
  PairingWindow window;

  // Closed when > 0 bonds, not in Egg, no QR card, no manual open
  TEST_ASSERT_FALSE(window.isOpen(1000, 1, false, false));
  TEST_ASSERT_FALSE(window.allowPrompt(1000, 1, false, false, false));

  // Open when 0 bonds
  TEST_ASSERT_TRUE(window.isOpen(1000, 0, false, false));
  TEST_ASSERT_TRUE(window.allowPrompt(1000, 0, false, false, false));

  // Open when in Egg
  TEST_ASSERT_TRUE(window.isOpen(1000, 2, true, false));

  // Open when QR card is visible
  TEST_ASSERT_TRUE(window.isOpen(1000, 2, false, true));

  // Manual open lasts 60 s
  window.open(5000);
  TEST_ASSERT_TRUE(window.isOpen(5000, 2, false, false));
  TEST_ASSERT_TRUE(window.isOpen(64999, 2, false, false));
  TEST_ASSERT_FALSE(window.isOpen(65000, 2, false, false));
}

void test_pairing_window_closes_on_bond_and_caps_at_four_slots() {
  PairingWindow window;
  window.open(1000);
  TEST_ASSERT_TRUE(window.allowPrompt(2000, 3, false, false, false));

  // 5th distinct bond is refused even when window is open
  TEST_ASSERT_FALSE(window.allowPrompt(2000, 4, false, false, false));
  // Re-pairing an existing slot is allowed when window is open
  TEST_ASSERT_TRUE(window.allowPrompt(2000, 4, false, false, true));

  // Completing a bond closes the manual window
  window.onBondSuccess();
  TEST_ASSERT_FALSE(window.isOpen(3000, 2, false, false));
}

void test_forget_all_confirm_gpio0_accepts_gpio14_cancels_and_timeout() {
  ForgetAllConfirm confirm;
  confirm.start(42, 1000);
  TEST_ASSERT_TRUE(confirm.pending());
  TEST_ASSERT_EQUAL_UINT32(42, confirm.requestId());

  RemoteInput idleButtons;
  // Idle tick keeps waiting
  ForgetAllConfirm::Result step = confirm.update(idleButtons, 2000);
  TEST_ASSERT_EQUAL(int(ForgetAllConfirm::Outcome::None), int(step.outcome));
  TEST_ASSERT_FALSE(step.consumed);

  // GPIO0 confirms
  RemoteInput happyBtn;
  happyBtn.happy = true;
  step = confirm.update(happyBtn, 3000);
  TEST_ASSERT_EQUAL(int(ForgetAllConfirm::Outcome::Confirmed), int(step.outcome));
  TEST_ASSERT_TRUE(step.consumed);
  TEST_ASSERT_EQUAL_UINT32(42, step.requestId);
  TEST_ASSERT_FALSE(confirm.pending());

  // GPIO14 cancels (Expired)
  confirm.start(43, 5000);
  RemoteInput idleBtn;
  idleBtn.idle = true;
  step = confirm.update(idleBtn, 6000);
  TEST_ASSERT_EQUAL(int(ForgetAllConfirm::Outcome::Expired), int(step.outcome));
  TEST_ASSERT_TRUE(step.consumed);
  TEST_ASSERT_EQUAL_UINT32(43, step.requestId);

  // 10 s timeout expires without consuming buttons
  confirm.start(44, 10000);
  step = confirm.update(idleButtons, 19999);
  TEST_ASSERT_EQUAL(int(ForgetAllConfirm::Outcome::None), int(step.outcome));
  step = confirm.update(idleButtons, 20000);
  TEST_ASSERT_EQUAL(int(ForgetAllConfirm::Outcome::Expired), int(step.outcome));
  TEST_ASSERT_FALSE(step.consumed);
  TEST_ASSERT_EQUAL_UINT32(44, step.requestId);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pairing_window_open_conditions_and_expiry);
  RUN_TEST(test_pairing_window_closes_on_bond_and_caps_at_four_slots);
  RUN_TEST(test_forget_all_confirm_gpio0_accepts_gpio14_cancels_and_timeout);
  return UNITY_END();
}
