#include <unity.h>

#include "xuna/hid/HidRateLimiter.h"

namespace {

using xuna::hid::HidRateLimiter;

void test_per_second_cap_rejects_fifth_press() {
  HidRateLimiter limiter;
  TEST_ASSERT_TRUE(limiter.allow(1000));
  TEST_ASSERT_TRUE(limiter.allow(1200));
  TEST_ASSERT_TRUE(limiter.allow(1400));
  TEST_ASSERT_TRUE(limiter.allow(1600));
  // 5th within 1000 ms of 1000 ms is rejected
  TEST_ASSERT_FALSE(limiter.allow(1999));

  // At 2000 ms, the 1000 ms entry expires
  TEST_ASSERT_TRUE(limiter.allow(2000));
}

void test_per_minute_cap_rejects_thirty_first_press() {
  HidRateLimiter limiter;
  uint32_t nowMs = 1000;
  for (uint8_t i = 0; i < HidRateLimiter::kMaxPerMinute; ++i) {
    TEST_ASSERT_TRUE(limiter.allow(nowMs));
    nowMs += 1500;  // 1.5 s apart -> well under 4/s, stays within 60 s
  }
  // 30 presses spanning [1000, 44500] ms -> 31st at 45000 ms is within 60 s of all 30
  TEST_ASSERT_FALSE(limiter.allow(45000));

  // At 61000 ms, the first press at 1000 ms is 60000 ms old and expires
  TEST_ASSERT_TRUE(limiter.allow(61000));
}

void test_reset_clears_history() {
  HidRateLimiter limiter;
  for (uint8_t i = 0; i < HidRateLimiter::kMaxPerSecond; ++i) {
    TEST_ASSERT_TRUE(limiter.allow(5000));
  }
  TEST_ASSERT_FALSE(limiter.allow(5100));
  limiter.reset();
  TEST_ASSERT_TRUE(limiter.allow(5100));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_per_second_cap_rejects_fifth_press);
  RUN_TEST(test_per_minute_cap_rejects_thirty_first_press);
  RUN_TEST(test_reset_clears_history);
  return UNITY_END();
}
