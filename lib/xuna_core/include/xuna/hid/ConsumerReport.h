#pragma once
#include <cstddef>
#include <cstdint>

namespace xuna {
namespace hid {

enum class ConsumerKey : uint8_t {
  None = 0,
  PlayPause = 1,
  Next = 2,
  Previous = 3,
  VolumeUp = 4,
  VolumeDown = 5,
  Mute = 6,
  Stop = 7
};

constexpr uint8_t kConsumerReportId = 3;
constexpr uint32_t kMinReleaseDelayMs = 30;

// Report map: one 8-bit input report, bits 0..6 = keys 1..7, bit 7 padding.
// Cross-checked against ESP-IDF esp_hid_device mediaReportMap (CC0 / Unlicense).
constexpr uint8_t kConsumerReportMap[] = {
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x03,  //   Report ID (3)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x07,  //   Report Count (7)
    0x09, 0xCD,  //   Usage (Play/Pause)
    0x09, 0xB5,  //   Usage (Scan Next Track)
    0x09, 0xB6,  //   Usage (Scan Previous Track)
    0x09, 0xE9,  //   Usage (Volume Increment)
    0x09, 0xEA,  //   Usage (Volume Decrement)
    0x09, 0xE2,  //   Usage (Mute)
    0x09, 0xB7,  //   Usage (Stop)
    0x81, 0x02,  //   Input (Data,Var,Abs)
    0x95, 0x01,  //   Report Count (1)
    0x81, 0x01,  //   Input (Const) padding
    0xC0         // End Collection
};

constexpr size_t kConsumerReportMapSize = sizeof(kConsumerReportMap);

inline uint8_t pressReport(ConsumerKey k) {
  const uint8_t raw = static_cast<uint8_t>(k);
  if (raw < 1 || raw > 7) return 0;
  return static_cast<uint8_t>(1u << (raw - 1));
}

constexpr uint8_t kReleaseReport = 0;

inline bool keyFromIndex(uint8_t index, ConsumerKey& out) {
  if (index < 1 || index > 7) {
    out = ConsumerKey::None;
    return false;
  }
  out = static_cast<ConsumerKey>(index);
  return true;
}

}  // namespace hid
}  // namespace xuna

