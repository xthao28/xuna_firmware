#include "BoardConfig.h"
#include <Arduino.h>
#include <cstdio>
#include <soc/esp32s3/spiram.h>
#include "DisplayManager.h"
#include "InputManager.h"
#include "BleManager.h"
#include <xuna/LocalBehavior.h>
#include <xuna/identity/IdentityService.h>
#include <xuna/life/BootSequence.h>
#include "NvsSnapshotStore.h"
#include "DeviceKey.h"

// Recovery and urgent commits stage several KB; ESP-IDF reports high water in bytes.
SET_LOOP_TASK_STACK_SIZE(16384)
namespace {
xuna::DisplayManager display;
xuna::InputManager input;
xuna::AnimationManager animation;
xuna::LocalBehavior localBehavior;
xuna::NvsSnapshotStore storage;
xuna::StoreSnapshotCommit storeCommit(storage);
xuna::IdentityService identity;
// No snapshot is written before the pet has an identity (Egg devices).
xuna::IdentityGatedCommit snapshotCommit(storeCommit,identity);
xuna::LifeCore core;
xuna::V1LifeCoreAdapter adapter(core,snapshotCommit,false,true);
xuna::BleManager ble;
xuna::DeviceKey deviceKey;
bool bleReady = false, lifeReady = false, persistReady = false;
bool displayReady = false;
uint32_t diagnosticsAt = 0;
uint32_t lastLoopUs = 0, maxLoopGapUs = 0;
xuna::VisualIntent lastVisual = xuna::VisualIntent::Unknown;
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
  const auto& n=adapter.last().needs;
  Serial.printf("%s t_s=%u activity=%s energy=%u energy_pct=%u stim=%u stim_pct=%u\n", tag, now / 1000,
                activityName(adapter.last().activity), n.energy, uint32_t(n.energy) * 100 / 65535,
                n.stimulation, uint32_t(n.stimulation) * 100 / 65535);
}
void logActivityChange(uint32_t now) {
  const uint8_t state=static_cast<uint8_t>(adapter.last().activity);
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
  const auto intent=adapter.visual();
  if(intent==lastVisual)return;
  lastVisual=intent;
  animation.setIntent(intent,0,now);
}
const char* recoveryName(xuna::LoadResult result) {
  switch(result) {
    case xuna::LoadResult::Ok: return "Ok";
    case xuna::LoadResult::Migrated: return "Migrated";
    case xuna::LoadResult::Missing: return "Missing";
    case xuna::LoadResult::TooNew: return "TooNew";
    case xuna::LoadResult::Corrupt: return "Corrupt";
    case xuna::LoadResult::MigrationFailed: return "MigrationFailed";
  }
  return "Unknown";
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
  const bool storageReady=storage.begin();
  // Boot-only scratch is static so recovery does not add another snapshot to the stack.
  static xuna::LifeSnapshotV2 recovered;
  struct HardwareEntropy : xuna::IRandom { uint32_t next() override { return esp_random(); } } entropy;
  const auto boot=xuna::BootSequence::run(storage,storageReady,snapshotCommit,core,identity,
                                          entropy,recovered,now);
  const auto& report=boot.recovery;
  lifeReady=boot.lifeReady;
  persistReady=boot.persistReady();
  Serial.printf("recover=%s slot=%d corrupt_slots=%u boot=%u ready=%u\n",
                recoveryName(report.result),report.slot,report.corruptSlots,boot.boot,lifeReady);
  if(identity.state()==xuna::IdentityState::Ready){
    Serial.print("PetId=");
    for(uint8_t byte:identity.record().petId)Serial.printf("%02x",byte);
    Serial.println();
  }
  adapter.begin(now);
  Serial.printf("learner=ProductLearner-v3 preferred quiet=%u play=%u\n",
                unsigned(adapter.preferred(0)),unsigned(adapter.preferred(1)));
  syncActivityVisual(now);logActivityChange(now);
  Serial.printf("stack tag=setup high_water_bytes=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  localBehavior.activity(now);
  bleReady = ble.begin();
  if (!bleReady) Serial.println("ERROR: BLE queue allocation failed");
  if(storageReady){
    const uint32_t keyStarted=micros();
    const auto key=deviceKey.begin(storage);
    static const char* const kKeyNames[]={"loaded","created","storage_failed","corrupt","crypto_failed"};
    Serial.printf("device_key=%s us=%u pub=",kKeyNames[static_cast<uint8_t>(key)],unsigned(micros()-keyStarted));
    if(deviceKey.ready())for(size_t i=0;i<9;++i)Serial.printf("%02x",deviceKey.publicKey()[i]);
    Serial.println();
    if(!deviceKey.ready())Serial.println("ERROR: device key unavailable; proofs report not_ready");
  }
  logMemory();
  lastLoopUs = micros();
}
void loop() {
  const uint32_t loopUs = micros();
  const uint32_t gap = loopUs - lastLoopUs;
  if (gap > maxLoopGapUs) maxLoopGapUs = gap;
  lastLoopUs = loopUs;
  const uint32_t now = millis();
  adapter.tick(now);
  const xuna::IdentityRecord* readyIdentity=
      identity.state()==xuna::IdentityState::Ready ? &identity.record() : nullptr;
  if (bleReady && ble.update(adapter,readyIdentity,deviceKey,now,displayReady&&lifeReady))localBehavior.activity(now);
  const auto events = input.update(now);
  if(lifeReady){
    if(events.happy&&adapter.localGreeting(now))Serial.println("button GPIO0: happy");
    if(events.idle&&adapter.localIdle(now))Serial.println("button GPIO14: idle");
    if(events.happy||events.idle)localBehavior.activity(now);
    uint8_t quietAction=0;
    if(localBehavior.choosePreferred(now,adapter.last().activity==xuna::Activity::Idle,
                                    static_cast<uint8_t>(adapter.preferred(0)),quietAction))
      adapter.localAction(static_cast<xuna::ActionId>(quietAction),now);
  }
  syncActivityVisual(now);
  logActivityChange(now);
  animation.update(now);
  if(displayReady)display.update(now,animation.state());
  if (bleReady) ble.stateChanged(animation.state(), now);
  if(persistReady&&adapter.checkpointDue(now)){
    const bool saved=adapter.checkpoint(now);
    Serial.printf("checkpoint=%s stack_high_water_bytes=%u\n",saved?"ok":"failed",
                  unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    if(!saved)Serial.println("ERROR: life snapshot save failed");
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
