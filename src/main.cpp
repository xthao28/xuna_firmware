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
#ifdef XUNA_E14
#include "E14Diagnostics.h"
#endif
#ifdef XUNA_NET
#if defined(XUNA_E14) && XUNA_E14
#error "XUNA_NET and the E14 network probe both drive Wi-Fi; build one of them"
#endif
#include "NetConfigStore.h"
#include "NetManager.h"
#include "CloudBackup.h"
#endif

// Recovery and urgent commits stage several KB; ESP-IDF reports high water in bytes.
SET_LOOP_TASK_STACK_SIZE(16384)
namespace {
xuna::DisplayManager display;
xuna::InputManager input;
xuna::AnimationManager animation;
xuna::LocalBehavior localBehavior;
xuna::PowerPacer pacer;
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
xuna::EggHatchHold eggHatchHold;
struct HardwareEntropy : xuna::IRandom { uint32_t next() override { return esp_random(); } } entropy;
#ifdef XUNA_E14
xuna::E14Diagnostics e14;  // E14 gate metrics; XUNA_E14=1 also starts the network probe
#endif
#ifdef XUNA_NET
xuna::NetConfigStore netStore;  // DC-2: Wi-Fi settings written by the app over BLE
xuna::NetManager net;
xuna::CloudBackup backup;  // DC-5: automatic projection backup over the cloud session
bool netReady = false;
#endif
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
  sceneMode = scenePreference.load();
  const auto savedBrightness = scenePreference.loadBrightness();
  const bool savedScreenSleep = scenePreference.loadScreenSleep();
  display.setBrightnessPreset(savedBrightness);
  pacer.setSleepEnabled(savedScreenSleep);
  displayReady = display.begin();
  Serial.printf("scene_mode=%s brightness=%u screen_sleep=%u\n",
                sceneName(sceneMode), unsigned(static_cast<uint8_t>(savedBrightness)),
                savedScreenSleep ? 1u : 0u);
  Serial.println(displayReady ? "Display driver initialized; confirm LCD visually." :
                                "ERROR: display dimensions/buffer allocation failed; input/logging continue.");
  const uint32_t now = millis();
  input.begin(now); animation.begin(now); diagnosticsAt = now;
  const bool storageReady=storage.begin();
  // Boot-only scratch is static so recovery does not add another snapshot to the stack.
  static xuna::LifeSnapshotV2 recovered;
  const auto boot=xuna::BootSequence::run(storage,storageReady,snapshotCommit,core,identity,
                                          entropy,recovered,now);
  adapter.attachIdentity(&identity,&storage,&entropy);
#if defined(XUNA_HID) && XUNA_HID
  adapter.attachHidControl(&ble.hid());
#endif
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
  pacer.touch(now);
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
#ifdef XUNA_NET
  {
    xuna::NetConfig stored;
    const bool configured=netStore.load(stored);
    netReady=net.begin(configured?&stored:nullptr,xuna::kFirmwareVersion);
    stored.wipe();
    const bool backupReady=backup.begin();
    Serial.printf("net configured=%u task=%u backup=%u\n",configured?1u:0u,netReady?1u:0u,backupReady?1u:0u);
  }
#endif
  logMemory();
#ifdef XUNA_E14
  e14.begin(millis());
#endif
  lastLoopUs = micros();
}
void loop() {
  const uint32_t loopUs = micros();
  const uint32_t gap = loopUs - lastLoopUs;
  if (gap > maxLoopGapUs) maxLoopGapUs = gap;
  lastLoopUs = loopUs;
  ++loops;
#ifdef XUNA_E14
  e14.loopGap(gap);
#endif
#if defined(XUNA_HID) && XUNA_HID
  ble.hid().recordLoopGap(gap);
#endif
  const uint32_t now = millis();
  adapter.tick(now);
  persistReady = lifeReady && identity.state() == xuna::IdentityState::Ready;
  const xuna::IdentityRecord* readyIdentity=
      identity.state()==xuna::IdentityState::Ready ? &identity.record() : nullptr;
  if (bleReady) {
    if (ble.update(adapter,readyIdentity,deviceKey,now,displayReady&&lifeReady))localBehavior.activity(now);
    if (ble.hadTraffic()){
      pacer.touch(now);
      if(displayReady&&display.screenSleeping())display.wake(now,animation.state());
    }
  }
  persistReady = lifeReady && identity.state() == xuna::IdentityState::Ready;
  readyIdentity = identity.state()==xuna::IdentityState::Ready ? &identity.record() : nullptr;
  {
    uint8_t bright=0;bool sleepOn=true;
    if(adapter.takeDisplaySettings(bright,sleepOn)){
      const auto preset=xuna::brightnessPresetFrom(bright);
      display.setBrightnessPreset(preset);
      pacer.setSleepEnabled(sleepOn);
      const bool savedB=scenePreference.saveBrightness(preset);
      const bool savedS=scenePreference.saveScreenSleep(sleepOn);
      pacer.touch(now);
      if(displayReady)display.wake(now,animation.state());
      Serial.printf("display_settings brightness=%u screen_sleep=%u saved=%u\n",
                    unsigned(static_cast<uint8_t>(preset)),sleepOn?1u:0u,(savedB&&savedS)?1u:0u);
    }
  }
#ifdef XUNA_NET
  if (bleReady && netReady) {
    xuna::NetOp op=xuna::NetOp::None;xuna::NetConfig config;
    if (ble.takeNetConfig(op,config)) {
      // Settings change only on the owner's request; the life snapshot is untouched.
      const bool saved = op==xuna::NetOp::Set ? netStore.save(config) : netStore.erase();
      if (op==xuna::NetOp::Set) net.apply(config); else net.forget();
      Serial.printf("net settings op=%u saved=%u\n",unsigned(op),saved?1u:0u);
    }
    config.wipe();
    static bool identified=false;
    if (!identified && readyIdentity) { net.identify(readyIdentity->petId); identified=true; }
    // One session proof per cloud connection, signed here: DeviceKey is not shared with core 0.
    net.serviceSigning(deviceKey,readyIdentity?readyIdentity->petId:nullptr,adapter.generation());
    backup.service(now,adapter,readyIdentity,net);
    ble.publishNet(net.status());
  }
#endif
  if (bleReady && displayReady) {
    const char* said=nullptr;
    switch (ble.takeSpeech(said)) {
      case 1:
        if(display.screenSleeping())display.wake(now,animation.state());
        display.showSpeech(said, now);
        pacer.touch(now);
        break;
      case 2: display.clearSpeech(); pacer.touch(now); break;
      default: break;
    }
    const char* hidSaid=nullptr;
    if(adapter.takeHidFeedback(hidSaid)){
      if(display.screenSleeping())display.wake(now,animation.state());
      display.showSpeech(hidSaid,now);
      display.showEffect(xuna::EffectKind::Sparkle,now);
      pacer.touch(now);
    }
  }
  const auto events = input.update(now);
  // While the screen is asleep or a card is shown, a single button press only wakes/closes it.
  const bool anyButton=events.happy||events.idle||events.happyHold||events.idleHold||events.bothHold;
  bool consumed=false;
  if(displayReady&&display.screenSleeping()&&anyButton){
    display.wake(now,animation.state());localBehavior.activity(now);pacer.touch(now);
    consumed=!events.bothHold;
    Serial.println("button: screen woke");
  }
  if(displayReady&&!consumed&&display.cardOpen()&&anyButton&&!events.bothHold){
    display.closeCard();consumed=true;localBehavior.activity(now);pacer.touch(now);
    Serial.println("button: card closed");
  }
#if defined(XUNA_HID) && XUNA_HID
  if(bleReady){
    const bool inEgg=identity.state()==xuna::IdentityState::Egg;
    const bool qrCardOpen=displayReady&&display.cardOpen();
    const uint16_t rawDay=core.clock().localDayIndex(core.mono64());
    const uint16_t currentDay=rawDay>0?rawDay:0xFFFF;
    const auto hidUi=ble.updateHid(inEgg,qrCardOpen,currentDay,events.happy,events.idle,events.happyHold,
                                   events.idleHold,events.bothHold,consumed,now);
    if(hidUi.wake&&displayReady&&display.screenSleeping())display.wake(now,animation.state());
    if(hidUi.closeCard&&displayReady&&display.cardOpen())display.closeCard();
    if(hidUi.clearSpeech&&displayReady)display.clearSpeech();
    if(hidUi.showSpeech&&displayReady)display.showSpeech(hidUi.speech,now);
    if(hidUi.sparkle&&displayReady)display.showEffect(xuna::EffectKind::Sparkle,now);
    if(hidUi.advanceScene&&readyIdentity){
      sceneMode=xuna::nextSceneMode(sceneMode);
      const bool saved=scenePreference.save(sceneMode);
      Serial.printf("button GPIO14: hold scene_mode=%s saved=%u\n",sceneName(sceneMode),saved?1u:0u);
      if(displayReady)display.showEffect(xuna::EffectKind::Sparkle,now);
    }
    if(hidUi.consumed){
      consumed=true;
      localBehavior.activity(now);
      pacer.touch(now);
    }
  }
#endif
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
    const bool inEgg = identity.state() == xuna::IdentityState::Egg &&
                       (!displayReady || (!display.screenSleeping() && !display.cardOpen()));
    const auto hatchHold = eggHatchHold.update(inEgg, input.idlePressed(), input.happyPressed(), now);
    if (hatchHold.started || hatchHold.stepChanged) {
      localBehavior.activity(now);
      pacer.touch(now);
      if (displayReady) {
        display.showEffect(xuna::EffectKind::Sparkle, now);
        static const char* const kHoldPrompts[] = {
            "Giữ nút để nở trứng... (3s)",
            "Giữ thêm chút nữa... (2s)",
            "Sắp nở rồi... (1s)",
        };
        if (hatchHold.step < 3) display.showSpeech(kHoldPrompts[hatchHold.step], now);
      }
    }
    if (hatchHold.canceled && displayReady) {
      display.clearSpeech();
    }
    if (hatchHold.triggered) {
      const bool hatched = adapter.hatchOnDevice();
      persistReady = lifeReady && identity.state() == xuna::IdentityState::Ready;
      readyIdentity = identity.state() == xuna::IdentityState::Ready ? &identity.record() : nullptr;
      localBehavior.activity(now);
      pacer.touch(now);
      if (displayReady) {
        display.showEffect(hatched ? xuna::EffectKind::Heart : xuna::EffectKind::Puff, now);
        display.showSpeech(hatched ? "Chào bạn! Mình vừa nở rồi nè!" : "Chưa nở được, thử lại nhé", now);
      }
      Serial.printf("button GPIO14: egg_hatch=%u\n", hatched ? 1u : 0u);
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
#if defined(XUNA_HID) && XUNA_HID
      if(shown&&bleReady)ble.hid().openPairingWindow(now);
#endif
      Serial.printf("button both: hold pairing_card=%u\n",shown?1u:0u);
    }
    if(anyButton){localBehavior.activity(now);pacer.touch(now);}
    uint8_t quietAction=0;
    // Idle for a while (51–69 s, by temperament): XUNA picks what to do itself; the owner may react.
    if((!displayReady||!display.screenSleeping())&&
       localBehavior.choosePreferred(now,adapter.last().activity==xuna::Activity::Idle,
                                     static_cast<uint8_t>(adapter.preferred(0)),quietAction,
                                     xuna::Expression::spontaneousWaitMs(core.personality())))
      adapter.localSpontaneous(now);
  }
  syncActivityVisual(now);
  logActivityChange(now);
  logTraitShift();
  // Track A: a variant LifeCore started (greeting, yawn, look around, bounce).
  const auto v=core.takeVariant();
  if(v!=xuna::VisualVariant::None){
    if(displayReady&&!display.screenSleeping())display.playVariant(v,now);
    static const char* const kVariantNames[]={"none","look_around","yawn_stretch","bounce_play","greet_nuzzle"};
    Serial.printf("variant=%s\n",kVariantNames[static_cast<uint8_t>(v)]);
  }
  // Personality expression (PE-2): tempo/mood set the sprite pace and how
  // long a reaction stays; decisions are unchanged.
  const auto style=xuna::Expression::styleFor(core.personality());
  animation.setReactionMs(style.reactionMs);
  animation.update(now);
  if(displayReady){
#if defined(XUNA_HID) && XUNA_HID
    const bool overlay=display.overlayOpen(now)||ble.hid().overlayKeepAlive();
#else
    const bool overlay=display.overlayOpen(now);
#endif
    const bool actionBusy=display.overlayOrActionBusy(now);
    display.setScreenPower(pacer.screenState(now,animation.state(),overlay),now,animation.state());
    display.setFramePeriod(pacer.framePeriodMs(now,animation.state(),actionBusy,style.framePeriodMs),now);
    display.setBackdrop(xuna::backdropFor(sceneMode,core.clock().dayPart(core.mono64())));
#if defined(XUNA_E14) || (defined(XUNA_HID) && XUNA_HID)
    const uint32_t drawsBefore=display.draws(),drawStarted=micros();
#endif
    display.update(now,animation.state());
#ifdef XUNA_E14
    if(display.draws()!=drawsBefore)e14.render(micros()-drawStarted);
#endif
#if defined(XUNA_HID) && XUNA_HID
    if(display.draws()!=drawsBefore)ble.hid().recordRender(micros()-drawStarted);
#endif
  }
  if (bleReady) ble.stateChanged(animation.state(), now);
  if(persistReady&&adapter.checkpointDue(now)){
    const bool saved=adapter.checkpoint(now);
    Serial.printf("checkpoint=%s stack_high_water_bytes=%u\n",saved?"ok":"failed",
                  unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    if(!saved)Serial.println("ERROR: life snapshot save failed");
  }
#if defined(XUNA_HID) && XUNA_HID
  const uint32_t loopPeriodMs = pacer.loopPeriodMs(
      now, animation.state(),
      (displayReady && display.visualBusy(now, animation.state())) || ble.hid().releasePending(),
      input.anyRawPressed(),
      (displayReady && display.overlayOpen(now)) || ble.hid().overlayKeepAlive());
#else
  const uint32_t loopPeriodMs = pacer.loopPeriodMs(
      now, animation.state(),
      displayReady && display.visualBusy(now, animation.state()),
      input.anyRawPressed(),
      displayReady && display.overlayOpen(now));
#endif
  if (uint32_t(now - diagnosticsAt) >= xuna::kDiagnosticsMs) {
    Serial.printf("scene=animated state=%s loop_gap_max_us=%u draws=%u draw_max_us=%u scene_max_us=%u\n",
                  (animation.state() == xuna::PetState::Idle ? "idle" : (animation.state() == xuna::PetState::Happy ? "happy" : (animation.state() == xuna::PetState::Sleep ? "sleep" : "curious"))), maxLoopGapUs, display.draws(), display.maxDrawUs(), display.maxSceneUs());
    // Heat check: loops per window and the chip's own sensor (not the board surface).
    Serial.printf("power loops=%u chip_temp_c=%.1f cpu_mhz=%u backlight=%u frame_ms=%u loop_ms=%u\n",
                  unsigned(loops), temperatureRead(), unsigned(getCpuFrequencyMhz()),
                  unsigned(display.backlight()), unsigned(display.framePeriod()), unsigned(loopPeriodMs));
    logNeeds("needs", now);
    logMemory(); diagnosticsAt = now;
    maxLoopGapUs = 0; loops = 0;
  }
  if (lifeReady && uint32_t(now - personaAt) >= 60000U) { logPersona(now, style); personaAt = now; }
#ifdef XUNA_E14
  e14.report(now,ble.e14Commands(),ble.e14NonOk(),ble.e14Dropped(),ble.e14MaxUs());
#endif
  // Sleep what is left of the period (a slow pass, e.g. a card push, only yields).
  const uint32_t spentMs = (micros() - loopUs) / 1000U;
  if (spentMs < loopPeriodMs) delay(loopPeriodMs - spentMs);
  else yield();
}
