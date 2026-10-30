#include <unity.h>

#include <xuna/hid/HidLinkRoles.h>

using namespace xuna::hid;

void third_connect_is_refused_and_advertising_tracks_capacity() {
  HidLinkRoles roles;
  TEST_ASSERT_TRUE(roles.shouldAdvertise());

  const auto c1 = roles.onConnect(1, 100);
  TEST_ASSERT_FALSE(c1.refuse);
  TEST_ASSERT_FALSE(c1.disconnect);
  TEST_ASSERT_TRUE(c1.advertise);
  TEST_ASSERT_EQUAL_UINT8(1, roles.linkCount());

  const auto c2 = roles.onConnect(2, 200);
  TEST_ASSERT_FALSE(c2.refuse);
  TEST_ASSERT_FALSE(c2.disconnect);
  TEST_ASSERT_FALSE(c2.advertise);
  TEST_ASSERT_EQUAL_UINT8(2, roles.linkCount());

  const auto c3 = roles.onConnect(3, 300);
  TEST_ASSERT_TRUE(c3.refuse);
  TEST_ASSERT_TRUE(c3.disconnect);
  TEST_ASSERT_EQUAL_UINT16(3, c3.disconnectConnId);
  TEST_ASSERT_FALSE(c3.advertise);
  TEST_ASSERT_EQUAL_UINT8(2, roles.linkCount());
}

void first_app_access_bumps_session_and_second_app_is_disconnected() {
  HidLinkRoles roles;
  roles.onConnect(1, 100);
  roles.onConnect(2, 200);

  const auto a1 = roles.onAppAccess(1);
  TEST_ASSERT_TRUE(a1.sessionBump);
  TEST_ASSERT_TRUE(a1.appConnected);
  TEST_ASSERT_FALSE(a1.disconnect);
  TEST_ASSERT_EQUAL_UINT16(1, roles.appConnId());

  // Repeated access on the same app link does not bump session again.
  const auto a1Again = roles.onAppAccess(1);
  TEST_ASSERT_FALSE(a1Again.sessionBump);
  TEST_ASSERT_TRUE(a1Again.appConnected);

  // Second link attempting app access is disconnected.
  const auto a2 = roles.onAppAccess(2);
  TEST_ASSERT_FALSE(a2.sessionBump);
  TEST_ASSERT_TRUE(a2.disconnect);
  TEST_ASSERT_EQUAL_UINT16(2, a2.disconnectConnId);
  TEST_ASSERT_TRUE(a2.appConnected);
  TEST_ASSERT_EQUAL_UINT16(1, roles.appConnId());
  TEST_ASSERT_TRUE(a2.advertise);
}

void encrypted_bonded_link_becomes_hid_host_and_second_host_is_disconnected() {
  HidLinkRoles roles;
  roles.onConnect(10, 100);
  roles.onConnect(20, 200);

  // Unbonded encryption does not grant HID host role.
  const auto unbonded = roles.onEncrypted(10, false);
  TEST_ASSERT_FALSE(unbonded.hidConnected);

  const auto h1 = roles.onEncrypted(10, true);
  TEST_ASSERT_TRUE(h1.hidConnected);
  TEST_ASSERT_FALSE(h1.disconnect);
  TEST_ASSERT_EQUAL_UINT16(10, roles.hidConnId());

  const auto h2 = roles.onEncrypted(20, true);
  TEST_ASSERT_TRUE(h2.disconnect);
  TEST_ASSERT_EQUAL_UINT16(20, h2.disconnectConnId);
  TEST_ASSERT_TRUE(h2.hidConnected);
  TEST_ASSERT_EQUAL_UINT16(10, roles.hidConnId());
  TEST_ASSERT_TRUE(h2.advertise);
}

void single_link_can_be_both_app_and_hid_host_for_iphone() {
  HidLinkRoles roles;
  roles.onConnect(7, 100);

  const auto app = roles.onAppAccess(7);
  TEST_ASSERT_TRUE(app.sessionBump);
  TEST_ASSERT_TRUE(app.appConnected);
  TEST_ASSERT_FALSE(app.hidConnected);

  const auto hid = roles.onEncrypted(7, true);
  TEST_ASSERT_FALSE(hid.sessionBump);
  TEST_ASSERT_TRUE(hid.appConnected);
  TEST_ASSERT_TRUE(hid.hidConnected);
  TEST_ASSERT_EQUAL_UINT16(7, roles.appConnId());
  TEST_ASSERT_EQUAL_UINT16(7, roles.hidConnId());
  TEST_ASSERT_EQUAL_UINT8(1, roles.linkCount());
  TEST_ASSERT_TRUE(roles.shouldAdvertise());
}

void app_disconnect_bumps_session_while_host_disconnect_does_not() {
  HidLinkRoles roles;
  roles.onConnect(1, 100);
  roles.onConnect(2, 200);
  roles.onAppAccess(1);
  roles.onEncrypted(2, true);
  TEST_ASSERT_FALSE(roles.shouldAdvertise());

  const auto dHost = roles.onDisconnect(2);
  TEST_ASSERT_FALSE(dHost.sessionBump);
  TEST_ASSERT_TRUE(dHost.appConnected);
  TEST_ASSERT_FALSE(dHost.hidConnected);
  TEST_ASSERT_TRUE(dHost.advertise);

  const auto dApp = roles.onDisconnect(1);
  TEST_ASSERT_TRUE(dApp.sessionBump);
  TEST_ASSERT_FALSE(dApp.appConnected);
  TEST_ASSERT_FALSE(dApp.hidConnected);
  TEST_ASSERT_TRUE(dApp.advertise);
  TEST_ASSERT_EQUAL_UINT8(0, roles.linkCount());
}

void squatter_without_role_is_disconnected_at_30s_while_roled_links_stay() {
  HidLinkRoles roles;
  roles.onConnect(1, 1000);  // will become app
  roles.onConnect(2, 1000);  // squatter
  roles.onAppAccess(1);

  const auto before = roles.tick(1000 + HidLinkRoles::kSquatterTimeoutMs - 1);
  TEST_ASSERT_FALSE(before.disconnect);
  TEST_ASSERT_EQUAL_UINT8(2, roles.linkCount());

  const auto atTimeout = roles.tick(1000 + HidLinkRoles::kSquatterTimeoutMs);
  TEST_ASSERT_TRUE(atTimeout.disconnect);
  TEST_ASSERT_EQUAL_UINT16(2, atTimeout.disconnectConnId);
  TEST_ASSERT_TRUE(atTimeout.appConnected);
  TEST_ASSERT_TRUE(atTimeout.advertise);
  TEST_ASSERT_EQUAL_UINT8(1, roles.linkCount());

  // App link stays connected well past 30 s.
  const auto later = roles.tick(1000 + 120000);
  TEST_ASSERT_FALSE(later.disconnect);
  TEST_ASSERT_TRUE(roles.appConnected());
}

void app_phone_and_dedicated_hid_host_both_encrypt_in_any_order_without_disconnect() {
  // Order 1: Mac/PC HID host connects and encrypts first (link 1), then App
  // phone connects, accesses custom service, and encrypts (link 2).
  {
    HidLinkRoles roles;
    roles.onConnect(1, 100);
    TEST_ASSERT_FALSE(roles.onEncrypted(1, true).disconnect);
    TEST_ASSERT_EQUAL_UINT16(1, roles.hidConnId());

    roles.onConnect(2, 200);
    TEST_ASSERT_FALSE(roles.onAppAccess(2).disconnect);
    const auto encApp = roles.onEncrypted(2, true);
    TEST_ASSERT_FALSE(encApp.disconnect);
    TEST_ASSERT_EQUAL_UINT16(2, roles.appConnId());
    TEST_ASSERT_EQUAL_UINT16(1, roles.hidConnId());
  }

  // Order 2: App phone connects, accesses custom service, and encrypts first
  // (link 1), then Mac/PC HID host connects and encrypts (link 2).
  {
    HidLinkRoles roles;
    roles.onConnect(1, 100);
    roles.onAppAccess(1);
    roles.onEncrypted(1, true);
    TEST_ASSERT_EQUAL_UINT16(1, roles.appConnId());
    TEST_ASSERT_EQUAL_UINT16(1, roles.hidConnId());

    roles.onConnect(2, 200);
    const auto encHost = roles.onEncrypted(2, true);
    TEST_ASSERT_FALSE(encHost.disconnect);
    TEST_ASSERT_EQUAL_UINT16(1, roles.appConnId());
    TEST_ASSERT_EQUAL_UINT16(2, roles.hidConnId());

    // When Mac/PC disconnects, encrypted App link reclaims hidConnId (e.g. iPhone).
    roles.onDisconnect(2);
    TEST_ASSERT_EQUAL_UINT16(1, roles.appConnId());
    TEST_ASSERT_EQUAL_UINT16(1, roles.hidConnId());
  }

  // Order 3: Reconnecting bonded App phone encrypts before its first GATT read
  // while Mac/PC is not yet encrypted, then Mac/PC encrypts and App reads 0002.
  {
    HidLinkRoles roles;
    roles.onConnect(1, 100);
    roles.onConnect(2, 150);
    roles.onEncrypted(1, true);  // link 1 encrypted first
    roles.onAppAccess(1);        // link 1 is the App
    const auto encHost = roles.onEncrypted(2, true);
    TEST_ASSERT_FALSE(encHost.disconnect);
    TEST_ASSERT_EQUAL_UINT16(1, roles.appConnId());
    TEST_ASSERT_EQUAL_UINT16(2, roles.hidConnId());
  }
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(third_connect_is_refused_and_advertising_tracks_capacity);
  RUN_TEST(first_app_access_bumps_session_and_second_app_is_disconnected);
  RUN_TEST(encrypted_bonded_link_becomes_hid_host_and_second_host_is_disconnected);
  RUN_TEST(single_link_can_be_both_app_and_hid_host_for_iphone);
  RUN_TEST(app_disconnect_bumps_session_while_host_disconnect_does_not);
  RUN_TEST(squatter_without_role_is_disconnected_at_30s_while_roled_links_stay);
  RUN_TEST(app_phone_and_dedicated_hid_host_both_encrypt_in_any_order_without_disconnect);
  return UNITY_END();
}

