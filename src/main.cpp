#include "BoardConfig.h"
#include <Arduino.h>
#include <soc/esp32s3/spiram.h>
#include "DisplayManager.h"
#include "InputManager.h"
#include "BleManager.h"
#include <xuna/LocalBehavior.h>
namespace {
xuna::DisplayManager display;
xuna::InputManager input;
xuna::AnimationManager animation;
xuna::LifeEngine life;
xuna::LocalBehavior localBehavior;
uint32_t bleActivityAt = 0;
xuna::LifeStorage storage;
xuna::BleManager ble;
bool bleReady = false;
uint32_t activeAt = 0, savedAt = 0;
bool displayReady = false;
uint32_t diagnosticsAt = 0;
uint32_t lastLoopUs = 0, maxLoopGapUs = 0;
void logMemory() {
  Serial.printf("heap_free=%u heap_min=%u psram_heap_total=%u psram_free=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getPsramSize(), ESP.getFreePsram());
}
}
void setup() {
  Serial.begin(115200); // No wait for USB host/Serial Monitor.
  Serial.printf("\nXUNA %s | MVP M05 | standard T-Display-S3 LCD\n", xuna::kFirmwareVersion);
  Serial.printf("flash=%u bytes psram_detected=%s\n", ESP.getFlashChipSize(), psramFound() ? "yes" : "no");
  const uint32_t psramChipBytes = psramFound() ? esp_spiram_get_size() : 0;
  Serial.printf("psram_chip=%u bytes\n", psramChipBytes);
  if (ESP.getFlashChipSize() != 16U * 1024U * 1024U || !psramFound() ||
      psramChipBytes != 8U * 1024U * 1024U)
    Serial.println("WARNING: memory differs from expected 16MB Flash / OPI PSRAM; verify board.");
  displayReady = display.begin();
  Serial.println(displayReady ? "Display driver initialized; confirm LCD visually." :
                                "ERROR: display dimensions/buffer allocation failed; input/logging continue.");
  const uint32_t now = millis();
  input.begin(now); animation.begin(now); diagnosticsAt = now;
  if (!storage.begin(life)) Serial.println("ERROR: life storage unavailable");
  activeAt = savedAt = now;
  localBehavior.activity(now);
  bleReady = ble.begin();
  if (!bleReady) Serial.println("ERROR: BLE queue allocation failed");
  logMemory();
  lastLoopUs = micros();
}
void loop() {
  const uint32_t loopUs = micros();
  const uint32_t gap = loopUs - lastLoopUs;
  if (gap > maxLoopGapUs) maxLoopGapUs = gap;
  lastLoopUs = loopUs;
  const uint32_t now = millis();
  if (bleReady) {
    ble.update(animation, life, storage, now, displayReady);
    if (ble.activityAt() != bleActivityAt) {
      bleActivityAt = ble.activityAt(); localBehavior.activity(now);
    }
  }
  const auto events = input.update(now);
  if (bleReady && (events.happy || events.idle)) ble.invalidateDecision();
  if (events.happy) {
    animation.setState(xuna::PetState::Happy, now);
    Serial.println("button GPIO0: happy");
  }
  if (events.idle) { // Explicit idle wins simultaneous presses.
    animation.setState(xuna::PetState::Idle, now);
    Serial.println("button GPIO14: idle");
  }
  if (events.happy || events.idle) localBehavior.activity(now);
  uint8_t quietAction = 0;
  if (localBehavior.choose(now, animation.state() == xuna::PetState::Idle, life, quietAction))
    animation.setState(static_cast<xuna::PetState>(quietAction), now);
  animation.update(now);
  if(displayReady)display.update(now,animation.state());
  if (bleReady) ble.stateChanged(animation.state(), now);
  const uint32_t activeSteps = uint32_t(now - activeAt) / 1000;
  life.activeSeconds += activeSteps; activeAt += activeSteps * 1000;
  if (uint32_t(now - savedAt) >= 300000) {
    if (!storage.save(life)) Serial.println("ERROR: life snapshot save failed");
    savedAt = now;
  }
  if (uint32_t(now - diagnosticsAt) >= xuna::kDiagnosticsMs) {
    Serial.printf("scene=animated state=%s loop_gap_max_us=%u draws=%u draw_max_us=%u\n",
                  (animation.state() == xuna::PetState::Idle ? "idle" : (animation.state() == xuna::PetState::Happy ? "happy" : (animation.state() == xuna::PetState::Sleep ? "sleep" : "curious"))), maxLoopGapUs, display.draws(), display.maxDrawUs());
    logMemory(); diagnosticsAt = now;
    maxLoopGapUs = 0;
  }
  yield();
}
