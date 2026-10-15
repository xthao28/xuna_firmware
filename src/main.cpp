#include "BoardConfig.h"
#include <Arduino.h>
#include <cstdio>
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
// Life-feel values approved for a board trial (2026-10-15).
xuna::ActivityFsm activity(xuna::lifeFeelThresholds());
xuna::Needs needs(xuna::lifeFeelTuning());
// Board trial (2026-10-15): learner v3 decides, v1 stays the durable record.
xuna::BoardLearner learner(1);
uint32_t bleActivityAt = 0;
xuna::LifeStorage storage;
xuna::BleManager ble;
bool bleReady = false;
uint32_t activeAt = 0, savedAt = 0;
bool displayReady = false;
uint32_t diagnosticsAt = 0;
uint32_t lastLoopUs = 0, maxLoopGapUs = 0;
uint8_t lastActivity = 255;
uint8_t loggedActivity = 255;
const char* activityName(xuna::Activity a) {
  switch(a) {
    case xuna::Activity::Booting: return "booting";
    case xuna::Activity::Idle: return "idle";
    case xuna::Activity::Observing: return "observing";
    case xuna::Activity::Reacting: return "reacting";
    case xuna::Activity::Playing: return "playing";
    case xuna::Activity::Resting: return "resting";
    case xuna::Activity::Sleeping: return "sleeping";
    case xuna::Activity::EvolutionTransition: return "evolution";
    case xuna::Activity::ErrorSafe: return "error_safe";
  }
  return "unknown";
}
// Life-feel board trial: one parseable line per call (energy/stim raw 0..65535).
void logNeeds(const char* tag, uint32_t now) {
  const auto& n=needs.state();
  Serial.printf("%s t_s=%u activity=%s energy=%u energy_pct=%u stim=%u stim_pct=%u\n", tag, now / 1000,
                activityName(activity.state()), n.energy, uint32_t(n.energy) * 100 / 65535,
                n.stimulation, uint32_t(n.stimulation) * 100 / 65535);
}
void logActivityChange(uint32_t now) {
  const uint8_t state=static_cast<uint8_t>(activity.state());
  if(loggedActivity==state) return;
  char head[48];
  snprintf(head, sizeof(head), "activity_change from=%s",
           loggedActivity==255 ? "none" : activityName(static_cast<xuna::Activity>(loggedActivity)));
  loggedActivity=state;
  logNeeds(head, now);
}
void logMemory() {
  Serial.printf("heap_free=%u heap_min=%u psram_heap_total=%u psram_free=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getPsramSize(), ESP.getFreePsram());
}
void syncActivityVisual(uint32_t now) {
  const auto state=activity.state();
  if(lastActivity==static_cast<uint8_t>(state)) return;
  lastActivity=static_cast<uint8_t>(state);
  switch(state) {
    case xuna::Activity::Idle:
    case xuna::Activity::Observing:
    case xuna::Activity::Resting: animation.setIntent(xuna::VisualIntent::Idle,0,now); break;
    case xuna::Activity::Sleeping: animation.setIntent(xuna::VisualIntent::Sleep,0,now); break;
    case xuna::Activity::ErrorSafe: animation.setIntent(xuna::VisualIntent::ErrorSafe,0,now); break;
    case xuna::Activity::Booting:
    case xuna::Activity::Reacting:
    case xuna::Activity::Playing:
    case xuna::Activity::EvolutionTransition: break;
  }
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
  input.begin(now); animation.begin(now); needs.reset(now); diagnosticsAt = now;
  const bool storageReady=storage.begin(life);
  if (!storageReady) Serial.println("ERROR: life storage unavailable");
  learner.reseed((uint64_t(esp_random()) << 32) | esp_random());
  learner.restore(life);
  Serial.printf("learner=v3-ucb (board trial) preferred quiet=%u play=%u\n",
                learner.preferred(0), learner.preferred(1));
  xuna::ActivityGuards boot; boot.restoreOk=storageReady; boot.energy=needs.state().energy;
  activity.request(storageReady ? xuna::Activity::Idle : xuna::Activity::ErrorSafe,boot,now);
  syncActivityVisual(now);
  logActivityChange(now);
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
  // The activity held since the previous loop decides decay versus recovery.
  needs.update(now, xuna::ActivityFsm::recoversEnergy(activity.state()));
  xuna::ActivityGuards tickGuards; tickGuards.energy=needs.state().energy; tickGuards.restoreOk=true;
  if (activity.tick(now,tickGuards)) syncActivityVisual(now);
  if (bleReady) {
    ble.update(animation, life, learner, storage, activity, needs, now, displayReady);
    if (ble.activityAt() != bleActivityAt) {
      bleActivityAt = ble.activityAt(); localBehavior.activity(now);
    }
  }
  const auto events = input.update(now);
  if (events.happy) {
    if (ble.requestLocalAction(animation,activity,needs,xuna::ActionId::Happy,now)) Serial.println("button GPIO0: happy");
  }
  if (events.idle) { // Explicit idle wins simultaneous presses.
    if (ble.requestLocalAction(animation,activity,needs,xuna::ActionId::Idle,now)) Serial.println("button GPIO14: idle");
  }
  if (events.happy || events.idle) localBehavior.activity(now);
  uint8_t quietAction = 0;
  if (localBehavior.choosePreferred(now, activity.state() == xuna::Activity::Idle, learner.preferred(0), quietAction))
    ble.requestLocalAction(animation,activity,needs,static_cast<xuna::ActionId>(quietAction),now);
  syncActivityVisual(now);
  logActivityChange(now);
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
    logNeeds("needs", now);
    logMemory(); diagnosticsAt = now;
    maxLoopGapUs = 0;
  }
  yield();
}
