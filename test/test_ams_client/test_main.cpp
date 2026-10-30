#include <cstring>
#include <unity.h>
#include <xuna/hid/AmsClientModel.h>

using xuna::hid::AmsClientModel;
using xuna::hid::AmsRemoteCommand;
using xuna::hid::ConsumerKey;

void setUp() {}
void tearDown() {}

namespace {

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

void parseUuidToLe(const char* str, uint8_t out[16]) {
  uint8_t be[16] = {};
  size_t idx = 0;
  for (size_t i = 0; str[i] != '\0' && idx < 16; ++i) {
    if (str[i] == '-') continue;
    const int hi = hexValue(str[i]);
    const int lo = hexValue(str[i + 1]);
    TEST_ASSERT_TRUE(hi >= 0 && lo >= 0);
    be[idx++] = static_cast<uint8_t>((hi << 4) | lo);
    ++i;
  }
  TEST_ASSERT_EQUAL_UINT32(16u, unsigned(idx));
  for (size_t i = 0; i < 16; ++i) {
    out[i] = be[15 - i];
  }
}

void test_ams_uuids_match_specification_bytes() {
  uint8_t expected[16] = {};

  parseUuidToLe(AmsClientModel::serviceUuidStr(), expected);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, AmsClientModel::serviceUuidLe(), 16);

  parseUuidToLe(AmsClientModel::remoteCommandUuidStr(), expected);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, AmsClientModel::remoteCommandUuidLe(), 16);

  parseUuidToLe(AmsClientModel::entityUpdateUuidStr(), expected);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, AmsClientModel::entityUpdateUuidLe(), 16);

  parseUuidToLe(AmsClientModel::entityAttributeUuidStr(), expected);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, AmsClientModel::entityAttributeUuidLe(), 16);
}

void test_consumer_key_to_ams_remote_command_mapping() {
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::TogglePlayPause),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::PlayPause)));
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::NextTrack),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::Next)));
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::PreviousTrack),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::Previous)));
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::VolumeUp),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::VolumeUp)));
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::VolumeDown),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::VolumeDown)));
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::Pause),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::Stop)));
  TEST_ASSERT_EQUAL_UINT8(
      uint8_t(AmsRemoteCommand::None),
      uint8_t(AmsClientModel::mapConsumerKey(ConsumerKey::Mute)));
}

void test_subscription_payloads_match_spec() {
  size_t trackLen = 0;
  const uint8_t* trackPayload = AmsClientModel::trackSubscriptionPayload(trackLen);
  TEST_ASSERT_EQUAL_UINT32(3u, unsigned(trackLen));
  TEST_ASSERT_EQUAL_UINT8(2u, trackPayload[0]);  // EntityIDTrack
  TEST_ASSERT_EQUAL_UINT8(2u, trackPayload[1]);  // TrackAttributeIDTitle
  TEST_ASSERT_EQUAL_UINT8(0u, trackPayload[2]);  // TrackAttributeIDArtist

  size_t playerLen = 0;
  const uint8_t* playerPayload = AmsClientModel::playerSubscriptionPayload(playerLen);
  TEST_ASSERT_EQUAL_UINT32(2u, unsigned(playerLen));
  TEST_ASSERT_EQUAL_UINT8(0u, playerPayload[0]);  // EntityIDPlayer
  TEST_ASSERT_EQUAL_UINT8(1u, playerPayload[1]);  // PlayerAttributeIDPlaybackInfo
}

void test_remote_command_notification_updates_supported_mask() {
  AmsClientModel model;
  model.reset();
  const uint8_t supported[] = {2, 3, 4};  // TogglePlayPause, NextTrack, PreviousTrack
  model.onRemoteCommandNotify(supported, sizeof(supported));

  TEST_ASSERT_TRUE(model.isCommandSupported(AmsRemoteCommand::TogglePlayPause));
  TEST_ASSERT_TRUE(model.isCommandSupported(AmsRemoteCommand::NextTrack));
  TEST_ASSERT_TRUE(model.isCommandSupported(AmsRemoteCommand::PreviousTrack));
  TEST_ASSERT_FALSE(model.isCommandSupported(AmsRemoteCommand::VolumeUp));
}

void test_entity_update_parses_playback_and_track_and_truncates_utf8_to_24_chars() {
  AmsClientModel model;
  model.reset();
  model.setReady(true);

  // Player PlaybackInfo: "1,1.0,12.34" -> playing = true
  const uint8_t playInfo[] = {0, 1, 0, '1', ',', '1', '.', '0', ',', '1', '2'};
  TEST_ASSERT_TRUE(model.onEntityUpdateNotify(playInfo, sizeof(playInfo)));
  TEST_ASSERT_TRUE(model.playing());

  // Player PlaybackInfo: "0,0.0,12.34" -> playing = false
  const uint8_t pauseInfo[] = {0, 1, 0, '0', ',', '0', '.', '0', ',', '1', '2'};
  TEST_ASSERT_TRUE(model.onEntityUpdateNotify(pauseInfo, sizeof(pauseInfo)));
  TEST_ASSERT_FALSE(model.playing());

  // Track Title (Vietnamese UTF-8) and Artist
  const char* titleStr = "Chúng Ta Của Hiện Tại";
  uint8_t titlePacket[64] = {2, 2, 0};
  const size_t titleLen = std::strlen(titleStr);
  std::memcpy(titlePacket + 3, titleStr, titleLen);
  TEST_ASSERT_TRUE(model.onEntityUpdateNotify(titlePacket, 3 + titleLen));
  TEST_ASSERT_TRUE(model.takeTrackChanged());
  TEST_ASSERT_FALSE(model.takeTrackChanged());

  const char* artistStr = "Sơn Tùng M-TP";
  uint8_t artistPacket[64] = {2, 0, 0};
  const size_t artistLen = std::strlen(artistStr);
  std::memcpy(artistPacket + 3, artistStr, artistLen);
  TEST_ASSERT_TRUE(model.onEntityUpdateNotify(artistPacket, 3 + artistLen));

  // Verify 24-code-point truncation does not split UTF-8 multi-byte characters
  char summary[72] = {};
  model.formatTrackSummary24(summary, sizeof(summary));
  // "Chúng Ta Của Hiện Tại - " is exactly 24 Unicode code points!
  TEST_ASSERT_EQUAL_STRING("Chúng Ta Của Hiện Tại - ", summary);

  char bubble[96] = {};
  model.formatRemoteBubble(1, bubble, sizeof(bubble));
  TEST_ASSERT_EQUAL_STRING("Remote > Chúng Ta Của Hiện Tại - ", bubble);
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ams_uuids_match_specification_bytes);
  RUN_TEST(test_consumer_key_to_ams_remote_command_mapping);
  RUN_TEST(test_subscription_payloads_match_spec);
  RUN_TEST(test_remote_command_notification_updates_supported_mask);
  RUN_TEST(test_entity_update_parses_playback_and_track_and_truncates_utf8_to_24_chars);
  return UNITY_END();
}

