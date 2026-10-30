#include <cstring>
#include <unity.h>

#include "xuna/hid/HidHostsCodec.h"

namespace {

using xuna::hid::HidHostsCodec;
using xuna::hid::HidHostsProjection;
using xuna::hid::HidHostSlotProjection;
using xuna::hid::HidSlotTable;

void test_encode_decode_round_trip() {
  HidHostsProjection in;
  in.hidEnabled = true;
  in.hostConnected = true;
  in.remoteActive = true;
  in.pairingWindowOpen = false;
  in.pairingPromptPending = false;
  in.connectedSlot = 1;
  in.bondCount = 2;

  in.slots[0].used = true;
  in.slots[0].connected = false;
  in.slots[0].isAppPhone = true;
  in.slots[0].lastSeenDays = 3;

  in.slots[1].used = true;
  in.slots[1].connected = true;
  in.slots[1].isAppPhone = false;
  in.slots[1].lastSeenDays = 0;

  uint8_t buf[HidHostsCodec::kSize] = {};
  TEST_ASSERT_TRUE(HidHostsCodec::encode(in, buf));
  TEST_ASSERT_EQUAL_UINT8(HidHostsCodec::kVersion, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(HidHostsCodec::kSize, buf[1]);

  HidHostsProjection out;
  TEST_ASSERT_TRUE(HidHostsCodec::decode(buf, sizeof(buf), out));
  TEST_ASSERT_EQUAL(in.hidEnabled, out.hidEnabled);
  TEST_ASSERT_EQUAL(in.hostConnected, out.hostConnected);
  TEST_ASSERT_EQUAL(in.remoteActive, out.remoteActive);
  TEST_ASSERT_EQUAL(in.pairingWindowOpen, out.pairingWindowOpen);
  TEST_ASSERT_EQUAL(in.pairingPromptPending, out.pairingPromptPending);
  TEST_ASSERT_EQUAL_UINT8(in.connectedSlot, out.connectedSlot);
  TEST_ASSERT_EQUAL_UINT8(in.bondCount, out.bondCount);

  for (size_t i = 0; i < HidHostsCodec::kMaxSlots; ++i) {
    TEST_ASSERT_EQUAL(in.slots[i].used, out.slots[i].used);
    TEST_ASSERT_EQUAL(in.slots[i].connected, out.slots[i].connected);
    TEST_ASSERT_EQUAL(in.slots[i].isAppPhone, out.slots[i].isAppPhone);
    TEST_ASSERT_EQUAL_UINT8(in.slots[i].lastSeenDays, out.slots[i].lastSeenDays);
  }
}

void test_decode_rejects_malformed_inputs() {
  HidHostsProjection in;
  uint8_t buf[HidHostsCodec::kSize] = {};
  TEST_ASSERT_TRUE(HidHostsCodec::encode(in, buf));

  HidHostsProjection out;
  TEST_ASSERT_FALSE(HidHostsCodec::decode(nullptr, sizeof(buf), out));
  TEST_ASSERT_FALSE(HidHostsCodec::decode(buf, sizeof(buf) - 1, out));
  TEST_ASSERT_FALSE(HidHostsCodec::encode(in, nullptr));

  // Bad version
  buf[0] = 2;
  TEST_ASSERT_FALSE(HidHostsCodec::decode(buf, sizeof(buf), out));
  buf[0] = 1;

  // Invalid connectedSlot (must be 0..3 or 0xFF)
  buf[3] = 4;
  TEST_ASSERT_FALSE(HidHostsCodec::decode(buf, sizeof(buf), out));
  buf[3] = 0xFF;

  // Invalid bondCount (> 4)
  buf[4] = 5;
  TEST_ASSERT_FALSE(HidHostsCodec::decode(buf, sizeof(buf), out));
  buf[4] = 0;

  // Reserved byte non-zero
  buf[13] = 1;
  TEST_ASSERT_FALSE(HidHostsCodec::decode(buf, sizeof(buf), out));
}

void test_slot_table_assignment_and_lowest_free_reuse() {
  HidSlotTable table;
  const uint8_t addr0[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x01};
  const uint8_t addr1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x02};
  const uint8_t addr2[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x03};
  const uint8_t addr3[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x04};
  const uint8_t addr4[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x05};

  TEST_ASSERT_EQUAL_INT(0, table.upsertAddress(addr0, true, 100));
  TEST_ASSERT_EQUAL_INT(1, table.upsertAddress(addr1, false, 101));
  TEST_ASSERT_EQUAL_INT(2, table.upsertAddress(addr2, false, 102));
  TEST_ASSERT_EQUAL_INT(3, table.upsertAddress(addr3, false, 103));
  TEST_ASSERT_EQUAL_UINT8(4, table.usedCount());

  // 5th distinct bond is refused
  TEST_ASSERT_FALSE(table.canAcceptBond(addr4));
  TEST_ASSERT_EQUAL_INT(-1, table.upsertAddress(addr4, false, 104));

  // Existing bond is still accepted and updates in place
  TEST_ASSERT_TRUE(table.canAcceptBond(addr1));
  TEST_ASSERT_EQUAL_INT(1, table.upsertAddress(addr1, false, 105));

  // Forgetting slot 1 frees it; next new bond reuses lowest free slot (1)
  TEST_ASSERT_TRUE(table.forgetSlot(1));
  TEST_ASSERT_EQUAL_UINT8(3, table.usedCount());
  TEST_ASSERT_TRUE(table.canAcceptBond(addr4));
  TEST_ASSERT_EQUAL_INT(1, table.upsertAddress(addr4, false, 106));
}

void test_slot_table_nvs_serialization_and_projection() {
  HidSlotTable table;
  const uint8_t appAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  const uint8_t pcAddr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

  TEST_ASSERT_EQUAL_INT(0, table.upsertAddress(appAddr, true, 10));
  TEST_ASSERT_EQUAL_INT(1, table.upsertAddress(pcAddr, false, 15));

  uint8_t blob[HidSlotTable::kBlobSize] = {};
  table.encodeBlob(blob);

  HidSlotTable restored;
  TEST_ASSERT_TRUE(restored.decodeBlob(blob, sizeof(blob)));
  TEST_ASSERT_EQUAL_UINT8(2, restored.usedCount());
  TEST_ASSERT_EQUAL_INT(0, restored.findByAddress(appAddr));
  TEST_ASSERT_EQUAL_INT(1, restored.findByAddress(pcAddr));

  HidHostsProjection proj = restored.buildProjection(
      true, 1, 0xFF, false, false, false, 20);
  TEST_ASSERT_EQUAL_UINT8(1, proj.connectedSlot);
  TEST_ASSERT_EQUAL_UINT8(2, proj.bondCount);
  TEST_ASSERT_TRUE(proj.slots[0].used);
  TEST_ASSERT_TRUE(proj.slots[0].isAppPhone);
  TEST_ASSERT_EQUAL_UINT8(10, proj.slots[0].lastSeenDays);
  TEST_ASSERT_TRUE(proj.slots[1].connected);
  TEST_ASSERT_EQUAL_UINT8(0, proj.slots[1].lastSeenDays);

  restored.forgetAll();
  TEST_ASSERT_EQUAL_UINT8(0, restored.usedCount());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_encode_decode_round_trip);
  RUN_TEST(test_decode_rejects_malformed_inputs);
  RUN_TEST(test_slot_table_assignment_and_lowest_free_reuse);
  RUN_TEST(test_slot_table_nvs_serialization_and_projection);
  return UNITY_END();
}
