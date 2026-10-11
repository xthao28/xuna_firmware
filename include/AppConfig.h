#pragma once
#include <cstdint>
namespace xuna {
constexpr const char* kFirmwareVersion = "0.2.0-mvp";
constexpr uint8_t kPowerPin = 15;
constexpr uint8_t kBacklightPin = 38;
constexpr uint8_t kHappyButtonPin = 0;
constexpr uint8_t kIdleButtonPin = 14;
constexpr uint8_t kRotation = 1; // 0..3; landscape by default
constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kIdleFrameMs = 400;
constexpr uint32_t kHappyFrameMs = 160;
constexpr uint32_t kHappyDurationMs = 2400;
constexpr uint32_t kDiagnosticsMs = 5000;
}
