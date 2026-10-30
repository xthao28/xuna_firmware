#include <unity.h>

#include <xuna/hid/ConsumerReport.h>

using namespace xuna::hid;

void each_consumer_key_maps_to_expected_bit() {
  TEST_ASSERT_EQUAL_UINT8(0x01, pressReport(ConsumerKey::PlayPause));
  TEST_ASSERT_EQUAL_UINT8(0x02, pressReport(ConsumerKey::Next));
  TEST_ASSERT_EQUAL_UINT8(0x04, pressReport(ConsumerKey::Previous));
  TEST_ASSERT_EQUAL_UINT8(0x08, pressReport(ConsumerKey::VolumeUp));
  TEST_ASSERT_EQUAL_UINT8(0x10, pressReport(ConsumerKey::VolumeDown));
  TEST_ASSERT_EQUAL_UINT8(0x20, pressReport(ConsumerKey::Mute));
  TEST_ASSERT_EQUAL_UINT8(0x40, pressReport(ConsumerKey::Stop));
  TEST_ASSERT_EQUAL_UINT8(0x00, kReleaseReport);
}

void none_and_out_of_range_keys_produce_zero_report() {
  TEST_ASSERT_EQUAL_UINT8(0x00, pressReport(ConsumerKey::None));
  TEST_ASSERT_EQUAL_UINT8(0x00, pressReport(static_cast<ConsumerKey>(8)));
  TEST_ASSERT_EQUAL_UINT8(0x00, pressReport(static_cast<ConsumerKey>(255)));
}

void consumer_report_map_has_expected_size_and_boundaries() {
  TEST_ASSERT_EQUAL_UINT32(37, uint32_t(sizeof(kConsumerReportMap)));
  TEST_ASSERT_EQUAL_UINT32(37, uint32_t(kConsumerReportMapSize));
  TEST_ASSERT_EQUAL_UINT8(0x05, kConsumerReportMap[0]);  // Usage Page
  TEST_ASSERT_EQUAL_UINT8(0x0C, kConsumerReportMap[1]);  // Consumer
  TEST_ASSERT_EQUAL_UINT8(0x85, kConsumerReportMap[6]);  // Report ID tag
  TEST_ASSERT_EQUAL_UINT8(kConsumerReportId, kConsumerReportMap[7]);
  TEST_ASSERT_EQUAL_UINT8(0xC0, kConsumerReportMap[sizeof(kConsumerReportMap) - 1]);
}

void key_from_index_accepts_one_through_seven_and_rejects_out_of_range() {
  ConsumerKey key = ConsumerKey::PlayPause;
  TEST_ASSERT_FALSE(keyFromIndex(0, key));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::None), uint8_t(key));

  for (uint8_t i = 1; i <= 7; ++i) {
    TEST_ASSERT_TRUE(keyFromIndex(i, key));
    TEST_ASSERT_EQUAL_UINT8(i, uint8_t(key));
  }

  TEST_ASSERT_FALSE(keyFromIndex(8, key));
  TEST_ASSERT_EQUAL_UINT8(uint8_t(ConsumerKey::None), uint8_t(key));
  TEST_ASSERT_FALSE(keyFromIndex(255, key));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(each_consumer_key_maps_to_expected_bit);
  RUN_TEST(none_and_out_of_range_keys_produce_zero_report);
  RUN_TEST(consumer_report_map_has_expected_size_and_boundaries);
  RUN_TEST(key_from_index_accepts_one_through_seven_and_rejects_out_of_range);
  return UNITY_END();
}
