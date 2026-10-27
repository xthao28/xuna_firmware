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
#include <xuna/personality/Expression.h>
#include "NvsSnapshotStore.h"
#include "DeviceKey.h"
#include "ScenePreference.h"

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
// Greeting learning on: owner reactions on the device teach it (D6 revised 2026-10-19).
xuna::V1LifeCoreAdapter adapter(core,snapshotCommit,true,true);
xuna::BleManager ble;
xuna::DeviceKey deviceKey;
xuna::ScenePreference scenePreference;
xuna::SceneMode sceneMode = xuna::SceneMode::Auto;
bool bleReady = false, lifeReady = false, persistReady = false;
bool displayReady = false;
uint32_t diagnosticsAt = 0, personaAt = 0;
uint16_t loggedJournal = 0;  // newest personality journal sequence already logged
uint32_t lastLoopUs = 0, maxLoopGapUs = 0, loops = 0;
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
// Personality PE-2 shadow (parseable; traits/mood Q1.14, drives 0..65535).
void logPersona(uint32_t now,const xuna::ExpressionStyle& style) {
  const auto& p=core.personality();
  const auto& g=p.genome();
  const auto& a=p.affect().state();
  const auto& d=p.drives().state();
  using xuna::TraitId;using xuna::Temperament;
  Serial.printf("persona t_s=%u cur=%d soc=%d play=%d tempo=%d sens=%d mood_v=%d mood_a=%d emo_v=%d comp=%u"
                " explore=%u social=%u playd=%u bored=%u sessions=%u evidence=%u shifts=%u frame_ms=%u react_ms=%u\n",
                now/1000,Temperament::effective(g,TraitId::Curiosity),Temperament::effective(g,TraitId::Sociability),
                Temperament::effective(g,TraitId::Playfulness),Temperament::effective(g,TraitId::Tempo),
                Temperament::effective(g,TraitId::Sensitivity),a.moodValence,a.moodArousal,a.emotionValence,
                a.competenceQ16,d.explore,d.social,d.play,d.boredom,unsigned(p.sessions()),unsigned(p.evidence()),
                unsigned(p.shifts()),style.framePeriodMs,style.reactionMs);
}
// A trait moved (TraitConsolidated, PE-3): the newest journal entry, once.
void logTraitShift() {
  const auto& e=core.personality().ledger().journal(0);
  if(e.sequence==loggedJournal)return;
  loggedJournal=e.sequence;
  if(e.sequence)Serial.printf("persona_shift seq=%u trait=%u delta=%d reason=%u source=%u sessions=%u\n",
                              e.sequence,e.trait,e.delta,e.reason,e.source,e.sessions);
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
const char* sceneName(xuna::SceneMode mode) {
  static const char* const kNames[]={"auto","day","evening","night","rain"};
  return kNames[static_cast<uint8_t>(mode)];
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
  setCpuFrequencyMhz(xuna::kCpuMhz);  // heat: before anything times itself
  Serial.begin(115200); // No wait for USB host/Serial Monitor.
  Serial.printf("\nXUNA %s | MVP M05 | standard T-Display-S3 LCD\n", xuna::kFirmwareVersion);
  Serial.printf("cpu_mhz=%u\n", unsigned(getCpuFrequencyMhz()));
  Serial.printf("flash=%u bytes psram_detected=%s\n", ESP.getFlashChipSize(), psramFound() ? "yes" : "no");
  const uint32_t psramChipBytes = psramFound() ? esp_spiram_get_size() : 0;
  Serial.printf("psram_chip=%u bytes\n", psramChipBytes);
  if (ESP.getFlashChipSize() != 16U * 1024U * 1024U || !psramFound() ||
      psramChipBytes != 8U * 1024U * 1024U)
    Serial.println("WARNING: memory differs from expected 16MB Flash / OPI PSRAM; verify board.");
  displayReady = display.begin();
  sceneMode = scenePreference.load();
  Serial.printf("scene_mode=%s\n", sceneName(sceneMode));
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
  loggedJournal=core.personality().ledger().journal(0).sequence;  // restored entries are not new
  Serial.printf("persona_restored bytes=%u sessions=%u evidence=%u journal=%u\n",
                unsigned(core.snapshot().personality.length),unsigned(core.personality().sessions()),
                unsigned(core.personality().evidence()),unsigned(loggedJournal));
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
  ++loops;
  const uint32_t now = millis();
  adapter.tick(now);
  const xuna::IdentityRecord* readyIdentity=
      identity.state()==xuna::IdentityState::Ready ? &identity.record() : nullptr;
  if (bleReady && ble.update(adapter,readyIdentity,deviceKey,now,displayReady&&lifeReady))localBehavior.activity(now);
  if (bleReady && displayReady) {
    const char* said=nullptr;
    switch (ble.takeSpeech(said)) {
      case 1: display.showSpeech(said, now); break;
      case 2: display.clearSpeech(); break;
      default: break;
    }
  }
  const auto events = input.update(now);
  // While a card is shown, any button only closes it.
  const bool anyButton=events.happy||events.idle||events.happyHold||events.idleHold||events.bothHold;
  bool consumed=false;
  if(displayReady&&display.cardOpen()&&anyButton&&!events.bothHold){
    display.closeCard();consumed=true;localBehavior.activity(now);
    Serial.println("button: card closed");
  }
  if(lifeReady&&!consumed){
    // A heart / puff above XUNA shows that the reaction taught it.
    if(events.happy&&adapter.localGreeting(now)){
      Serial.println(adapter.lastReactionApplied()?"button GPIO0: pet (reaction)":"button GPIO0: greeting");
      if(adapter.lastReactionApplied())display.showEffect(xuna::EffectKind::Heart,now);
    }
    if(events.idle&&adapter.localIdle(now)){
      Serial.println(adapter.lastReactionApplied()?"button GPIO14: shoo (reaction), idle":"button GPIO14: idle");
      if(adapter.lastReactionApplied())display.showEffect(xuna::EffectKind::Puff,now);
    }
    // GPIO14 hold cycles the window scene. Off before hatch: in Egg the
    // GPIO14 hold is reserved for hatching (S10 protocol freeze).
    if(events.idleHold&&readyIdentity){
      sceneMode=xuna::nextSceneMode(sceneMode);
      const bool saved=scenePreference.save(sceneMode);
      Serial.printf("button GPIO14: hold scene_mode=%s saved=%u\n",sceneName(sceneMode),saved?1u:0u);
      display.showEffect(xuna::EffectKind::Sparkle,now);
    }
    // GPIO0 hold: status card (age, energy, what XUNA learned, usual arrival).
    if(events.happyHold&&displayReady){
      const bool shown=display.showStatus(xuna::makeStatusCard(core,adapter.last().needs.energy,animation.state(),
                                                                 readyIdentity?readyIdentity->petId:nullptr),now);
      Serial.printf("button GPIO0: hold status_card=%u\n",shown?1u:0u);
    }
    // Both buttons 3 s: pairing QR (PetId + device key prefix; nothing secret).
    if(events.bothHold&&displayReady){
      char uri[xuna::kPairingUriSize],shortId[9]="";
      bool shown=false;
      if(readyIdentity&&deviceKey.ready()&&xuna::pairingUri(readyIdentity->petId,deviceKey.publicKey(),uri,sizeof(uri))){
        xuna::hexPrefix(readyIdentity->petId,4,shortId);
        shown=display.showPairing(uri,shortId,now);
      }
      Serial.printf("button both: hold pairing_card=%u\n",shown?1u:0u);
    }
    if(anyButton)localBehavior.activity(now);
    uint8_t quietAction=0;
    // Idle for a while (51–69 s, by temperament): XUNA picks what to do itself; the owner may react.
    if(localBehavior.choosePreferred(now,adapter.last().activity==xuna::Activity::Idle,
                                    static_cast<uint8_t>(adapter.preferred(0)),quietAction,
                                    xuna::Expression::spontaneousWaitMs(core.personality())))
      adapter.localSpontaneous(now);
  }
  syncActivityVisual(now);
  logActivityChange(now);
  logTraitShift();
  // Track A: a variant LifeCore started (greeting, yawn, look around, bounce).
  if(const auto v=core.takeVariant();v!=xuna::VisualVariant::None){
    if(displayReady)display.playVariant(v,now);
    static const char* const kVariantNames[]={"none","look_around","yawn_stretch","bounce_play","greet_nuzzle"};
    Serial.printf("variant=%s\n",kVariantNames[static_cast<uint8_t>(v)]);
  }
  // Personality expression (PE-2): tempo/mood set the sprite pace and how
  // long a reaction stays; decisions are unchanged.
  const auto style=xuna::Expression::styleFor(core.personality());
  animation.setReactionMs(style.reactionMs);
  animation.update(now);
  if(displayReady){
    display.setFramePeriod(style.framePeriodMs,now);
    display.setBackdrop(xuna::backdropFor(sceneMode,core.clock().dayPart(core.mono64())));
    display.update(now,animation.state());
  }
  if (bleReady) ble.stateChanged(animation.state(), now);
  if(persistReady&&adapter.checkpointDue(now)){
    const bool saved=adapter.checkpoint(now);
    Serial.printf("checkpoint=%s stack_high_water_bytes=%u\n",saved?"ok":"failed",
                  unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    if(!saved)Serial.println("ERROR: life snapshot save failed");
  }
  if (uint32_t(now - diagnosticsAt) >= xuna::kDiagnosticsMs) {
    Serial.printf("scene=animated state=%s loop_gap_max_us=%u draws=%u draw_max_us=%u scene_max_us=%u\n",
                  (animation.state() == xuna::PetState::Idle ? "idle" : (animation.state() == xuna::PetState::Happy ? "happy" : (animation.state() == xuna::PetState::Sleep ? "sleep" : "curious"))), maxLoopGapUs, display.draws(), display.maxDrawUs(), display.maxSceneUs());
    // Heat check: loops per window and the chip's own sensor (not the board surface).
    Serial.printf("power loops=%u chip_temp_c=%.1f cpu_mhz=%u backlight=%u\n", unsigned(loops), temperatureRead(),
                  unsigned(getCpuFrequencyMhz()), unsigned(display.backlight()));
    logNeeds("needs", now);
    logMemory(); diagnosticsAt = now;
    maxLoopGapUs = 0; loops = 0;
  }
  if (lifeReady && uint32_t(now - personaAt) >= 60000U) { logPersona(now, style); personaAt = now; }
  // Sleep what is left of the period (a slow pass, e.g. a card push, only yields).
  const uint32_t spentMs = (micros() - loopUs) / 1000U;
  if (spentMs < xuna::kLoopPeriodMs) delay(xuna::kLoopPeriodMs - spentMs);
  else yield();
}
