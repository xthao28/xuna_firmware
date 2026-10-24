#pragma once
#include <cstdint>
namespace xuna {
constexpr const char* kFirmwareVersion = "0.2.0-mvp";
constexpr uint8_t kPowerPin = 15;
constexpr uint8_t kBacklightPin = 38;
constexpr uint8_t kHappyButtonPin = 0;
constexpr uint8_t kIdleButtonPin = 14;
constexpr uint8_t kRotation = 1; // 0..3; landscape by default
constexpr uint32_t kDiagnosticsMs = 5000;
// Loop pacing (heat, 2026-10-24): one pass every 10 ms, the rest is idle so
// the CPU waits for interrupts instead of spinning (~25,000 passes/s before).
// Frames are 85–115 ms and buttons debounce over 30 ms, so nothing visible changes.
constexpr uint32_t kLoopPeriodMs = 10;
}
