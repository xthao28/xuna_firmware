#include <Arduino.h>
#include "DisplayManager.h"
#include "InputManager.h"
namespace {
xuna::DisplayManager display;
xuna::InputManager input;
xuna::AnimationManager animation;
bool displayReady = false;
uint32_t diagnosticsAt = 0, frames = 0, renderTotalUs = 0, renderMaxUs = 0;
uint32_t lastLoopUs = 0, maxLoopGapUs = 0;
void logMemory() {
  Serial.printf("heap_free=%u heap_min=%u psram_total=%u psram_free=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getPsramSize(), ESP.getFreePsram());
}
}
void setup() {
  Serial.begin(115200); // No wait for USB host/Serial Monitor.
  Serial.printf("\nXUNA %s | M01 | standard T-Display-S3 LCD\n", xuna::kFirmwareVersion);
  Serial.printf("flash=%u bytes psram_detected=%s\n", ESP.getFlashChipSize(), psramFound() ? "yes" : "no");
  if (ESP.getFlashChipSize() != 16U * 1024U * 1024U || !psramFound() ||
      ESP.getPsramSize() != 8U * 1024U * 1024U)
    Serial.println("WARNING: memory differs from expected 16MB Flash / OPI PSRAM; verify board.");
  displayReady = display.begin();
  Serial.println(displayReady ? "Display driver initialized; confirm LCD visually." :
                                "ERROR: sprite allocation failed; display disabled, input/logging continue.");
  const uint32_t now = millis();
  input.begin(now); animation.begin(now); diagnosticsAt = now;
  logMemory();
  lastLoopUs = micros();
}
void loop() {
  const uint32_t loopUs = micros();
  const uint32_t gap = loopUs - lastLoopUs;
  if (gap > maxLoopGapUs) maxLoopGapUs = gap;
  lastLoopUs = loopUs;
  const uint32_t now = millis();
  const auto events = input.update(now);
  if (events.happy) {
    animation.setState(xuna::PetState::Happy, now);
    Serial.println("button GPIO0: happy");
  }
  if (events.idle) { // Explicit idle wins simultaneous presses.
    animation.setState(xuna::PetState::Idle, now);
    Serial.println("button GPIO14: idle");
  }
  if (animation.update(now) && displayReady) {
    const uint32_t start = micros();
    display.render(animation.state(), animation.frame());
    const uint32_t elapsed = micros() - start;
    ++frames; renderTotalUs += elapsed;
    if (elapsed > renderMaxUs) renderMaxUs = elapsed;
  }
  if (uint32_t(now - diagnosticsAt) >= xuna::kDiagnosticsMs) {
    const uint32_t window = now - diagnosticsAt;
    Serial.printf("state=%s frames=%u fps=%.2f render_avg_us=%u render_max_us=%u loop_gap_max_us=%u\n",
                  animation.state() == xuna::PetState::Idle ? "idle" : "happy", frames,
                  frames * 1000.0 / window, frames ? renderTotalUs / frames : 0,
                  renderMaxUs, maxLoopGapUs);
    logMemory(); diagnosticsAt = now;
    frames = renderTotalUs = renderMaxUs = maxLoopGapUs = 0;
  }
  yield();
}
