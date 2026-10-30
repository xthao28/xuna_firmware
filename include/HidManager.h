#pragma once
#if defined(XUNA_HID) && XUNA_HID
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <xuna/base/LatencyHistogram.h>
#include <xuna/hid/ConsumerReport.h>
#include <xuna/hid/HidHostsCodec.h>
#include <xuna/hid/HidLinkRoles.h>
#include <xuna/hid/PairingConfirm.h>
#include <xuna/hid/PairingWindow.h>
#include <xuna/hid/RemoteMode.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>

namespace xuna {

// Board-side BLE HID Consumer Control peripheral glue and UDC-2 host manager.
// Keeps all state-machine decisions in pure xuna::hid::* headers and copies
// Bluedroid callback data through lock-free queues.
class HidManager : public IHidControl {
 public:
  static constexpr uint32_t kDiagIntervalMs = 10000;
  static constexpr uint32_t kBubbleRefreshMs = 4000;

  struct UiAction {
    bool wake = false;
    bool closeCard = false;
    bool showSpeech = false;
    bool clearSpeech = false;
    bool sparkle = false;
    bool advanceScene = false;
    bool consumed = false;
    char speech[96] = {};
  };

  bool begin(BLEServer* server, BLEService* customService,
             const char* customServiceUuid) {
    server_ = server;
    gapQueue_ = xQueueCreate(8, sizeof(GapEvent));
    gattsQueue_ = xQueueCreate(16, sizeof(GattsEvent));
    if (!gapQueue_ || !gattsQueue_) return false;

    instancePtr() = this;
    BLEDevice::setCustomGapHandler(&HidManager::gapCallback);
    BLEDevice::setCustomGattsHandler(&HidManager::gattsCallback);

    serviceStartHandle_ = customService ? customService->getHandle() : 0;
    serviceEndHandle_ = uint16_t(serviceStartHandle_ + 56);

    hid_ = new BLEHIDDevice(server);
    // BLEHIDDevice::manufacturer(std::string) in Arduino-ESP32 2.0.14 requires
    // calling manufacturer() first so the 0x2a29 characteristic is created.
    hid_->manufacturer()->setValue("XUNA");
    hid_->pnp(0x01, 0x02E5, 0x0001, 0x0100);
    hid_->hidInfo(0x00, 0x02);
    hid_->reportMap(const_cast<uint8_t*>(hid::kConsumerReportMap),
                    sizeof(hid::kConsumerReportMap));
    input_ = hid_->inputReport(hid::kConsumerReportId);

    if (auto* devInfo = hid_->deviceInfo()) {
      const uint16_t h = devInfo->getHandle();
      if (h > serviceStartHandle_) serviceEndHandle_ = h;
    }
    if (auto* hidSvc = hid_->hidService()) {
      hidStartHandle_ = hidSvc->getHandle();
      hidEndHandle_ = uint16_t(hidStartHandle_ + 32);
    }

    // HOGP §7.1: require link encryption on all HID characteristics/descriptors.
    constexpr esp_gatt_perm_t kEncPerm =
        ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED;
    if (auto* hidSvc = hid_->hidService()) {
      if (auto* c = hidSvc->getCharacteristic(BLEUUID(static_cast<uint16_t>(0x2a4a))))
        c->setAccessPermissions(kEncPerm);
      if (auto* c = hidSvc->getCharacteristic(BLEUUID(static_cast<uint16_t>(0x2a4b))))
        c->setAccessPermissions(kEncPerm);
    }
    if (auto* c = hid_->hidControl()) c->setAccessPermissions(kEncPerm);
    if (auto* c = hid_->protocolMode()) c->setAccessPermissions(kEncPerm);
    if (input_) {
      input_->setAccessPermissions(kEncPerm);
      if (auto* d = input_->getDescriptorByUUID(BLEUUID(static_cast<uint16_t>(0x2902))))
        d->setAccessPermissions(kEncPerm);
      if (auto* d = input_->getDescriptorByUUID(BLEUUID(static_cast<uint16_t>(0x2908))))
        d->setAccessPermissions(kEncPerm);
    }

    hid_->startServices();

    // LE Secure Connections + MITM (Numeric Comparison via DisplayYesNo) + Bonding.
    // Do NOT register BLESecurityCallbacks (avoids synchronous onConfirmPIN in BT task).
    security_.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    security_.setCapability(ESP_IO_CAP_IO);
    security_.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security_.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security_.setKeySize(16);
    uint8_t onlySpecified = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH,
                                   &onlySpecified, sizeof(uint8_t));

    loadSlotsFromNvs();
    reconcileBondsWithSlots();

    // Advertising payload must fit in 31 bytes:
    // flags (3) + 128-bit XUNA UUID (18) + 16-bit 0x1812 UUID (4) + appearance
    // 0x03C0 (4) = 29 B. Device name stays in scan response.
    auto* adv = BLEDevice::getAdvertising();
    BLEAdvertisementData advData;
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    advData.setCompleteServices(BLEUUID(customServiceUuid));
    advData.setCompleteServices(BLEUUID(static_cast<uint16_t>(0x1812)));
    advData.setAppearance(GENERIC_HID);
    adv->setAdvertisementData(advData);

    BLEAdvertisementData scanResp;
    scanResp.setName("XUNA");
    adv->setScanResponseData(scanResp);
    return true;
  }

  // IHidControl implementation (UDC-2 opcodes 12 and 13).
  bool canSendConsumerKey() const override {
    return hidConnected() && input_ != nullptr;
  }

  uint8_t sendConsumerKey(uint8_t usageIndex, uint8_t source, uint32_t now) override {
    hid::ConsumerKey key = hid::ConsumerKey::None;
    if (!hid::keyFromIndex(usageIndex, key)) return V1LifeCoreAdapter::kInvalidArgument;
    if (!sendKey(key, now)) return V1LifeCoreAdapter::kNotReady;
    Serial.printf("hid cmd_key=0x%02x src=%u\n",
                  unsigned(static_cast<uint8_t>(key)), unsigned(source));
    return V1LifeCoreAdapter::kOk;
  }

  uint8_t handleHidHosts(uint8_t subOp, uint8_t slot, uint32_t requestId,
                         uint32_t now) override {
    if (subOp == 1) {
      if (slot >= hid::HidHostsCodec::kMaxSlots) return V1LifeCoreAdapter::kInvalidArgument;
      forgetHostSlot(slot);
      Serial.printf("hid hosts_cmd=forget_slot slot=%u\n", unsigned(slot));
      return V1LifeCoreAdapter::kOk;
    }
    if (subOp == 2) {
      forgetAllConfirm_.start(requestId, now);
      forgetAllPromptPending_ = true;
      Serial.printf("hid hosts_cmd=forget_all_prompt req=%u\n", unsigned(requestId));
      return V1LifeCoreAdapter::kDeferred;
    }
    if (subOp == 3) {
      openPairingWindow(now);
      pairingWindowPromptPending_ = true;
      Serial.println("hid hosts_cmd=open_pairing_window");
      return V1LifeCoreAdapter::kOk;
    }
    return V1LifeCoreAdapter::kInvalidArgument;
  }

  void openPairingWindow(uint32_t nowMs) {
    pairingWindow_.open(nowMs);
  }

  bool takeDeferredResult(uint32_t& requestId, uint8_t& result) {
    if (!deferredReady_) return false;
    deferredReady_ = false;
    requestId = deferredRequestId_;
    result = deferredResult_;
    return true;
  }

  // Targeted GATT notification to the app connection ID only (UDC-2 §5.1).
  void notifyApp(BLECharacteristic* chr, const uint8_t* data, size_t len) const {
    if (!chr || !data || len == 0) return;
    const uint16_t connId = appConnId();
    const esp_gatt_if_t gattsIf = gattsIf_.load();
    if (!appConnected() || connId == hid::HidLinkRoles::kNoConnId ||
        gattsIf == ESP_GATT_IF_NONE) {
      return;
    }
    auto* cccd = reinterpret_cast<BLE2902*>(
        chr->getDescriptorByUUID(BLEUUID(static_cast<uint16_t>(0x2902))));
    if (cccd && !cccd->getNotifications()) return;
    esp_ble_gatts_send_indicate(gattsIf, connId, chr->getHandle(), uint16_t(len),
                                const_cast<uint8_t*>(data), false);
  }

  void encodeHidHosts(uint32_t nowMs, uint8_t out[hid::HidHostsCodec::kSize]) {
    const uint8_t hostSlot = connectedHidSlot();
    const uint8_t appSlot = connectedAppSlot();
    const bool winOpen = pairingWindow_.isOpen(
        nowMs, slots_.usedCount(), inEgg_, qrCardOpen_);
    const hid::HidHostsProjection proj = slots_.buildProjection(
        true, hostSlot, appSlot, remote_.active(), winOpen,
        pairing_.pending() || forgetAllConfirm_.pending(), currentDay_);
    hid::HidHostsCodec::encode(proj, out);
  }

  void onConnect(BLEServer* server, uint16_t connId, const esp_bd_addr_t bda,
                 uint32_t nowMs, std::atomic<uint32_t>& session,
                 std::atomic<bool>& appConn, std::atomic<uint16_t>& central) {
    hid::HidLinkRoles::Action r;
    bool adv = false;
    portENTER_CRITICAL(&mux_);
    r = roles_.onConnect(connId, nowMs);
    if (!r.refuse) {
      rememberPeerLocked(connId, bda);
      appConn.store(roles_.appConnected());
      central.store(roles_.appConnId());
      if (r.sessionBump) ++session;
      adv = roles_.shouldAdvertise();
    }
    portEXIT_CRITICAL(&mux_);

    if (r.disconnect && r.disconnectConnId != hid::HidLinkRoles::kNoConnId) {
      server->disconnect(r.disconnectConnId);
      return;
    }
    if (connId <= 8) {
      esp_ble_tx_power_set(static_cast<esp_ble_power_type_t>(
                               ESP_BLE_PWR_TYPE_CONN_HDL0 + connId),
                           ESP_PWR_LVL_N0);
    }
    uint8_t remoteBda[ESP_BD_ADDR_LEN];
    std::memcpy(remoteBda, bda, ESP_BD_ADDR_LEN);
    server->updateConnParams(remoteBda, 0x10, 0x20, 0, 400);
    if (adv) BLEDevice::startAdvertising();
  }

  void onDisconnect(uint16_t connId, std::atomic<uint32_t>& session,
                    std::atomic<bool>& appConn, std::atomic<uint16_t>& central) {
    hid::HidLinkRoles::Action r;
    bool adv = false;
    portENTER_CRITICAL(&mux_);
    r = roles_.onDisconnect(connId);
    forgetPeerLocked(connId);
    appConn.store(roles_.appConnected());
    central.store(roles_.appConnId());
    if (r.sessionBump) ++session;
    adv = roles_.shouldAdvertise();
    portEXIT_CRITICAL(&mux_);

    if (adv) BLEDevice::startAdvertising();
  }

  bool onAppAccessFromCallback(uint16_t connId, std::atomic<uint32_t>& session,
                               std::atomic<bool>& appConn,
                               std::atomic<uint16_t>& central) {
    if (connId == hid::HidLinkRoles::kNoConnId) return appConn.load();
    hid::HidLinkRoles::Action r;
    bool isApp = false;
    portENTER_CRITICAL(&mux_);
    r = roles_.onAppAccess(connId);
    appConn.store(roles_.appConnected());
    central.store(roles_.appConnId());
    if (r.sessionBump) ++session;
    isApp = (roles_.appConnId() == connId);
    portEXIT_CRITICAL(&mux_);

    if (r.disconnect && r.disconnectConnId != hid::HidLinkRoles::kNoConnId &&
        server_) {
      server_->disconnect(r.disconnectConnId);
    }
    return isApp;
  }

  bool shouldAdvertise() const {
    portENTER_CRITICAL(&mux_);
    const bool s = roles_.shouldAdvertise();
    portEXIT_CRITICAL(&mux_);
    return s;
  }

  bool appConnected() const {
    portENTER_CRITICAL(&mux_);
    const bool c = roles_.appConnected();
    portEXIT_CRITICAL(&mux_);
    return c;
  }

  uint16_t appConnId() const {
    portENTER_CRITICAL(&mux_);
    const uint16_t id = roles_.appConnId();
    portEXIT_CRITICAL(&mux_);
    return id;
  }

  bool hidConnected() const {
    portENTER_CRITICAL(&mux_);
    const bool c = roles_.hidConnected();
    portEXIT_CRITICAL(&mux_);
    return c;
  }

  uint8_t linkCount() const {
    portENTER_CRITICAL(&mux_);
    const uint8_t n = roles_.linkCount();
    portEXIT_CRITICAL(&mux_);
    return n;
  }

  uint16_t hidConnId() const {
    portENTER_CRITICAL(&mux_);
    const uint16_t id = roles_.hidConnId();
    portEXIT_CRITICAL(&mux_);
    return id;
  }

  bool releasePending() const { return releasePending_; }
  bool overlayKeepAlive() const {
    return pairing_.pending() || forgetAllConfirm_.pending() || remote_.active();
  }

  void recordLoopGap(uint32_t us) { gaps_.add(us); }
  void recordRender(uint32_t us) {
    if (us > renderMaxUs_) renderMaxUs_ = us;
  }
  void recordBleCmdResult(uint8_t result) {
    if (result != 0 && result != V1LifeCoreAdapter::kDeferred) ++bleCmdFail_;
  }

  bool sendKey(hid::ConsumerKey key, uint32_t nowMs) {
    const uint8_t report = hid::pressReport(key);
    if (!report || !input_) return false;
    const uint16_t connId = hidConnId();
    const esp_gatt_if_t gattsIf = gattsIf_.load();
    if (!hidConnected() || connId == hid::HidLinkRoles::kNoConnId ||
        gattsIf == ESP_GATT_IF_NONE) {
      return false;
    }
    if (releasePending_) {
      uint8_t rel = hid::kReleaseReport;
      esp_ble_gatts_send_indicate(gattsIf, releaseConnId_, input_->getHandle(),
                                  1, &rel, false);
      releasePending_ = false;
    }
    uint8_t press = report;
    const esp_err_t err = esp_ble_gatts_send_indicate(
        gattsIf, connId, input_->getHandle(), 1, &press, false);
    if (err != ESP_OK) return false;
    releasePending_ = true;
    releaseConnId_ = connId;
    releaseAtMs_ = nowMs + hid::kMinReleaseDelayMs;
    return true;
  }

  // Runs on the main loop task: drains GAP/GATTS queues, drives HidLinkRoles
  // squatter timeout, PairingConfirm, ForgetAllConfirm, RemoteMode, scheduled
  // key releases, and serial commands/diagnostics.
  UiAction update(bool inEgg, bool qrCardOpen, uint16_t currentDay, bool happy,
                  bool idle, bool happyHold, bool idleHold, bool bothHold,
                  bool buttonsAlreadyConsumed, uint32_t nowMs,
                  std::atomic<uint32_t>& session, std::atomic<bool>& appConn,
                  std::atomic<uint16_t>& central) {
    inEgg_ = inEgg;
    qrCardOpen_ = qrCardOpen;
    currentDay_ = currentDay;

    UiAction ui;
    drainQueues(nowMs, ui, session, appConn, central);

    if (pairingWindowPromptPending_) {
      pairingWindowPromptPending_ = false;
      ui.wake = true;
      setSpeech(ui, "Đang mở ghép đôi Bluetooth (60s)", nowMs);
    }
    if (forgetAllPromptPending_) {
      forgetAllPromptPending_ = false;
      ui.wake = true;
      setSpeech(ui, "Xóa hết máy đã ghép? GPIO0 đồng ý · GPIO14 hủy", nowMs);
    }

    // 1. Squatter 30 s timeout check.
    {
      hid::HidLinkRoles::Action tr;
      portENTER_CRITICAL(&mux_);
      tr = roles_.tick(nowMs);
      appConn.store(roles_.appConnected());
      central.store(roles_.appConnId());
      if (tr.sessionBump) ++session;
      portEXIT_CRITICAL(&mux_);
      if (tr.disconnect &&
          tr.disconnectConnId != hid::HidLinkRoles::kNoConnId && server_) {
        Serial.printf("hid squatter_timeout conn=%u\n",
                      unsigned(tr.disconnectConnId));
        server_->disconnect(tr.disconnectConnId);
      }
    }

    // 2. Scheduled >= 30 ms key release report.
    if (releasePending_ && uint32_t(nowMs - releaseAtMs_) < 0x80000000U) {
      releasePending_ = false;
      const esp_gatt_if_t gattsIf = gattsIf_.load();
      if (hidConnected() && gattsIf != ESP_GATT_IF_NONE && input_) {
        uint8_t rel = hid::kReleaseReport;
        esp_ble_gatts_send_indicate(gattsIf, releaseConnId_,
                                    input_->getHandle(), 1, &rel, false);
      }
    }

    // 3. PairingConfirm takes priority over all button handling while pending.
    hid::RemoteInput rawButtons;
    rawButtons.happy = happy;
    rawButtons.idle = idle;
    rawButtons.happyHold = happyHold;
    rawButtons.idleHold = idleHold;
    rawButtons.bothHold = bothHold;
    const auto pr = pairing_.update(rawButtons, nowMs);
    if (pr.reply != hid::PairingReply::None) {
      sendConfirmReply(pr.replyToken, pr.reply == hid::PairingReply::Accept);
      ui.clearSpeech = true;
      Serial.printf("hid pairing_reply=%s\n",
                    pr.reply == hid::PairingReply::Accept ? "accept" : "reject");
    }
    if (pr.consumed) {
      ui.consumed = true;
    }

    // 3b. ForgetAllConfirm (10 s physical confirmation for opcode 13 arg=2).
    if (!ui.consumed && forgetAllConfirm_.pending()) {
      const auto fr = forgetAllConfirm_.update(rawButtons, nowMs);
      if (fr.consumed) ui.consumed = true;
      if (fr.outcome == hid::ForgetAllConfirm::Outcome::Confirmed) {
        const uint16_t hConn = hidConnId();
        if (hConn != hid::HidLinkRoles::kNoConnId && server_) {
          server_->disconnect(hConn);
        }
        forgetAllBonds();
        slots_.forgetAll();
        saveSlotsToNvs();
        deferredRequestId_ = fr.requestId;
        deferredResult_ = V1LifeCoreAdapter::kOk;
        deferredReady_ = true;
        setSpeech(ui, "Đã xóa thiết bị đã ghép", nowMs);
        Serial.println("hid forget_all confirmed=1");
      } else if (fr.outcome == hid::ForgetAllConfirm::Outcome::Expired) {
        deferredRequestId_ = fr.requestId;
        deferredResult_ = V1LifeCoreAdapter::kExpired;
        deferredReady_ = true;
        ui.clearSpeech = true;
        Serial.println("hid forget_all confirmed=0");
      }
    }

    // 4. RemoteMode: always ticked for 60 s inactivity timeout and HID
    // disconnect; button inputs are suppressed if already consumed by pairing,
    // forget-all confirm, wake, or card close.
    const bool passButtons = !buttonsAlreadyConsumed && !ui.consumed;
    hid::RemoteInput remoteButtons;
    remoteButtons.happy = passButtons && happy;
    remoteButtons.idle = passButtons && idle;
    remoteButtons.happyHold = passButtons && happyHold;
    remoteButtons.idleHold = passButtons && idleHold;
    remoteButtons.bothHold = passButtons && bothHold;
    const auto rr = remote_.update(remoteButtons, hidConnected(), nowMs, inEgg);
    const unsigned slotNum = unsigned(
        connectedHidSlot() < hid::HidHostsCodec::kMaxSlots
            ? connectedHidSlot() + 1u
            : 1u);
    if (rr.enter) {
      ui.wake = true;
      ui.sparkle = true;
      char buf[48];
      std::snprintf(buf, sizeof(buf), "Remote > máy %u", slotNum);
      setSpeech(ui, buf, nowMs);
      Serial.printf("hid remote=on slot=%u\n", slotNum);
    }
    if (rr.key != hid::ConsumerKey::None) {
      const bool sent = sendKey(rr.key, nowMs);
      ui.sparkle = true;
      char buf[48];
      std::snprintf(buf, sizeof(buf), "Remote > máy %u", slotNum);
      setSpeech(ui, buf, nowMs);
      Serial.printf("hid remote_key=0x%02x sent=%u\n",
                    unsigned(static_cast<uint8_t>(rr.key)), sent ? 1u : 0u);
    }
    if (rr.stop) {
      sendKey(hid::ConsumerKey::Stop, nowMs);
      setSpeech(ui, "Đã dừng", nowMs);
      Serial.println("hid remote=stop");
    }
    if (rr.exit) {
      if (!rr.stop && !ui.showSpeech) ui.clearSpeech = true;
      Serial.printf("hid remote=off advance_scene=%u stop=%u\n",
                    rr.advanceScene ? 1u : 0u, rr.stop ? 1u : 0u);
    }
    if (rr.advanceScene) ui.advanceScene = true;
    if (rr.consumed) ui.consumed = true;

    // 5. Keep the prompt / Remote bubble alive while active so SpeechBubble's
    // 5-8 s page timer does not hide a 30 s pairing prompt or 60 s Remote session.
    if (!ui.showSpeech && !ui.clearSpeech &&
        uint32_t(nowMs - lastBubbleRefreshMs_) >= kBubbleRefreshMs) {
      if (pairing_.pending()) {
        formatPasskeyPrompt(ui.speech, sizeof(ui.speech), pairing_.passkey());
        ui.showSpeech = true;
        lastBubbleRefreshMs_ = nowMs;
      } else if (forgetAllConfirm_.pending()) {
        setSpeech(ui, "Xóa hết máy đã ghép? GPIO0 đồng ý · GPIO14 hủy", nowMs);
      } else if (remote_.active()) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Remote > máy %u", slotNum);
        setSpeech(ui, buf, nowMs);
      }
    }

    // 6. Non-blocking serial commands and 10 s diagnostics line.
    serviceSerial(nowMs, ui);
    if (uint32_t(nowMs - lastDiagMs_) >= kDiagIntervalMs) {
      lastDiagMs_ = nowMs;
      printStatusLine();
    }
    return ui;
  }

  void printStatusLine() const {
    Serial.printf(
        "hid links=%u app=%u host=%u bonds=%u slots=%u heap_int_min=%u "
        "heap_int_largest=%u psram_free=%u "
        "loop_p50=%u loop_p99=%u loop_max=%u render_max=%u "
        "ble_cmd_fail=%u resets=%u\n",
        unsigned(linkCount()), appConnected() ? 1u : 0u,
        hidConnected() ? 1u : 0u, unsigned(bondCount()),
        unsigned(slots_.usedCount()),
        unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                  MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        unsigned(gaps_.percentileUs(500)), unsigned(gaps_.percentileUs(990)),
        unsigned(gaps_.maxUs()), unsigned(renderMaxUs_), unsigned(bleCmdFail_),
        unsigned(resets_));
  }

 private:
  struct GapEvent {
    enum class Type : uint8_t {
      NumericConfirm,
      PasskeyNotify,
      AuthComplete
    } type;
    esp_bd_addr_t bda;
    uint32_t passkey;
    bool success;
    uint8_t failReason;
  };

  struct GattsEvent {
    enum class Type : uint8_t { Connect, AppAccess, HidAccess } type;
    uint16_t connId;
    esp_bd_addr_t bda;
  };

  struct PeerEntry {
    bool active = false;
    uint16_t connId = hid::HidLinkRoles::kNoConnId;
    esp_bd_addr_t bda = {};
  };

  static HidManager*& instancePtr() {
    static HidManager* p = nullptr;
    return p;
  }

  static void gapCallback(esp_gap_ble_cb_event_t event,
                          esp_ble_gap_cb_param_t* param) {
    HidManager* inst = instancePtr();
    if (!inst || !inst->gapQueue_ || !param) return;
    GapEvent ev = {};
    switch (event) {
      case ESP_GAP_BLE_NC_REQ_EVT:
        ev.type = GapEvent::Type::NumericConfirm;
        std::memcpy(ev.bda, param->ble_security.key_notif.bd_addr,
                    ESP_BD_ADDR_LEN);
        ev.passkey = param->ble_security.key_notif.passkey;
        xQueueSend(inst->gapQueue_, &ev, 0);
        break;
      case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        ev.type = GapEvent::Type::PasskeyNotify;
        std::memcpy(ev.bda, param->ble_security.key_notif.bd_addr,
                    ESP_BD_ADDR_LEN);
        ev.passkey = param->ble_security.key_notif.passkey;
        xQueueSend(inst->gapQueue_, &ev, 0);
        break;
      case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ev.type = GapEvent::Type::AuthComplete;
        std::memcpy(ev.bda, param->ble_security.auth_cmpl.bd_addr,
                    ESP_BD_ADDR_LEN);
        ev.success = param->ble_security.auth_cmpl.success;
        ev.failReason = param->ble_security.auth_cmpl.fail_reason;
        xQueueSend(inst->gapQueue_, &ev, 0);
        break;
      default:
        break;
    }
  }

  static void gattsCallback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                            esp_ble_gatts_cb_param_t* param) {
    HidManager* inst = instancePtr();
    if (!inst) return;
    if (gatts_if != ESP_GATT_IF_NONE) inst->gattsIf_.store(gatts_if);
    if (!inst->gattsQueue_ || !param) return;
    GattsEvent ev = {};
    switch (event) {
      case ESP_GATTS_CONNECT_EVT:
        ev.type = GattsEvent::Type::Connect;
        ev.connId = param->connect.conn_id;
        std::memcpy(ev.bda, param->connect.remote_bda, ESP_BD_ADDR_LEN);
        xQueueSend(inst->gattsQueue_, &ev, 0);
        break;
      case ESP_GATTS_READ_EVT:
        if (inst->inCustomService(param->read.handle)) {
          ev.type = GattsEvent::Type::AppAccess;
          ev.connId = param->read.conn_id;
          xQueueSend(inst->gattsQueue_, &ev, 0);
        } else if (inst->inHidService(param->read.handle)) {
          ev.type = GattsEvent::Type::HidAccess;
          ev.connId = param->read.conn_id;
          xQueueSend(inst->gattsQueue_, &ev, 0);
        }
        break;
      case ESP_GATTS_WRITE_EVT:
        if (inst->inCustomService(param->write.handle)) {
          ev.type = GattsEvent::Type::AppAccess;
          ev.connId = param->write.conn_id;
          xQueueSend(inst->gattsQueue_, &ev, 0);
        } else if (inst->inHidService(param->write.handle)) {
          ev.type = GattsEvent::Type::HidAccess;
          ev.connId = param->write.conn_id;
          xQueueSend(inst->gattsQueue_, &ev, 0);
        }
        break;
      default:
        break;
    }
  }

  bool inCustomService(uint16_t handle) const {
    return serviceStartHandle_ > 0 && handle >= serviceStartHandle_ &&
           handle < serviceEndHandle_;
  }

  bool inHidService(uint16_t handle) const {
    return hidStartHandle_ > 0 && handle >= hidStartHandle_ &&
           handle < hidEndHandle_;
  }

  void rememberPeerLocked(uint16_t connId, const esp_bd_addr_t bda) {
    for (size_t i = 0; i < 4; ++i) {
      if (peers_[i].active && peers_[i].connId == connId) {
        std::memcpy(peers_[i].bda, bda, ESP_BD_ADDR_LEN);
        return;
      }
    }
    for (size_t i = 0; i < 4; ++i) {
      if (!peers_[i].active) {
        peers_[i].active = true;
        peers_[i].connId = connId;
        std::memcpy(peers_[i].bda, bda, ESP_BD_ADDR_LEN);
        return;
      }
    }
  }

  void forgetPeerLocked(uint16_t connId) {
    for (size_t i = 0; i < 4; ++i) {
      if (peers_[i].active && peers_[i].connId == connId) {
        peers_[i].active = false;
        peers_[i].connId = hid::HidLinkRoles::kNoConnId;
      }
    }
  }

  uint16_t connIdForBdaLocked(const esp_bd_addr_t bda) const {
    for (size_t i = 0; i < 4; ++i) {
      if (peers_[i].active &&
          std::memcmp(peers_[i].bda, bda, ESP_BD_ADDR_LEN) == 0) {
        return peers_[i].connId;
      }
    }
    return hid::HidLinkRoles::kNoConnId;
  }

  bool bdaForConnIdLocked(uint16_t connId, esp_bd_addr_t outBda) const {
    if (connId == hid::HidLinkRoles::kNoConnId) return false;
    for (size_t i = 0; i < 4; ++i) {
      if (peers_[i].active && peers_[i].connId == connId) {
        std::memcpy(outBda, peers_[i].bda, ESP_BD_ADDR_LEN);
        return true;
      }
    }
    return false;
  }

  uint8_t connectedHidSlot() const {
    esp_bd_addr_t bda = {};
    bool found = false;
    portENTER_CRITICAL(&mux_);
    if (roles_.hidConnected()) {
      found = bdaForConnIdLocked(roles_.hidConnId(), bda);
    }
    portEXIT_CRITICAL(&mux_);
    if (!found) return hid::HidHostsCodec::kNoSlot;
    const int idx = slots_.findByAddress(bda);
    return idx >= 0 ? uint8_t(idx) : hid::HidHostsCodec::kNoSlot;
  }

  uint8_t connectedAppSlot() const {
    esp_bd_addr_t bda = {};
    bool found = false;
    portENTER_CRITICAL(&mux_);
    if (roles_.appConnected()) {
      found = bdaForConnIdLocked(roles_.appConnId(), bda);
    }
    portEXIT_CRITICAL(&mux_);
    if (!found) return hid::HidHostsCodec::kNoSlot;
    const int idx = slots_.findByAddress(bda);
    return idx >= 0 ? uint8_t(idx) : hid::HidHostsCodec::kNoSlot;
  }

  void loadSlotsFromNvs() {
    Preferences prefs;
    if (!prefs.begin("xuna_hid", true)) return;
    uint8_t blob[hid::HidSlotTable::kBlobSize] = {};
    if (prefs.getBytesLength("slots") == sizeof(blob) &&
        prefs.getBytes("slots", blob, sizeof(blob)) == sizeof(blob)) {
      slots_.decodeBlob(blob, sizeof(blob));
    }
    prefs.end();
  }

  void saveSlotsToNvs() {
    Preferences prefs;
    if (!prefs.begin("xuna_hid", false)) return;
    uint8_t blob[hid::HidSlotTable::kBlobSize] = {};
    slots_.encodeBlob(blob);
    prefs.putBytes("slots", blob, sizeof(blob));
    prefs.end();
  }

  void reconcileBondsWithSlots() {
    int devNum = esp_ble_get_bond_device_num();
    if (devNum < 0) devNum = 0;
    if (devNum > 16) devNum = 16;
    esp_ble_bond_dev_t list[16] = {};
    if (devNum > 0 && esp_ble_get_bond_device_list(&devNum, list) != ESP_OK) {
      return;
    }
    bool changed = false;
    for (uint8_t s = 0; s < hid::HidHostsCodec::kMaxSlots; ++s) {
      const auto& e = slots_.entry(s);
      if (!e.used) continue;
      bool present = false;
      for (int i = 0; i < devNum; ++i) {
        if (hid::HidSlotTable::hashAddress(list[i].bd_addr) == e.addrHash) {
          present = true;
          break;
        }
      }
      if (!present) {
        slots_.forgetSlot(s);
        changed = true;
      }
    }
    for (int i = 0; i < devNum; ++i) {
      if (slots_.findByAddress(list[i].bd_addr) < 0) {
        if (slots_.canAcceptBond(list[i].bd_addr)) {
          slots_.upsertAddress(list[i].bd_addr, false, currentDay_);
          changed = true;
        } else {
          esp_ble_remove_bond_device(list[i].bd_addr);
        }
      }
    }
    if (changed) saveSlotsToNvs();
  }

  void forgetHostSlot(uint8_t slot) {
    if (slot >= hid::HidHostsCodec::kMaxSlots) return;
    const auto& entry = slots_.entry(slot);
    const uint32_t hash = entry.used ? entry.addrHash : 0;
    if (hash != 0) {
      uint16_t toDisc = hid::HidLinkRoles::kNoConnId;
      portENTER_CRITICAL(&mux_);
      for (size_t i = 0; i < 4; ++i) {
        if (peers_[i].active &&
            hid::HidSlotTable::hashAddress(peers_[i].bda) == hash) {
          toDisc = peers_[i].connId;
          break;
        }
      }
      portEXIT_CRITICAL(&mux_);
      if (toDisc != hid::HidLinkRoles::kNoConnId && server_) {
        server_->disconnect(toDisc);
      }

      int devNum = esp_ble_get_bond_device_num();
      if (devNum > 0) {
        if (devNum > 16) devNum = 16;
        esp_ble_bond_dev_t list[16] = {};
        if (esp_ble_get_bond_device_list(&devNum, list) == ESP_OK) {
          for (int i = 0; i < devNum; ++i) {
            if (hid::HidSlotTable::hashAddress(list[i].bd_addr) == hash) {
              esp_ble_remove_bond_device(list[i].bd_addr);
            }
          }
        }
      }
    }
    if (slots_.forgetSlot(slot)) {
      saveSlotsToNvs();
    }
  }

  uint8_t allocToken(const esp_bd_addr_t bda) {
    const uint8_t t = uint8_t(nextToken_ & 3u);
    ++nextToken_;
    std::memcpy(tokenBda_[t], bda, ESP_BD_ADDR_LEN);
    return t;
  }

  void sendConfirmReply(uint8_t token, bool accept) {
    esp_bd_addr_t bda;
    std::memcpy(bda, tokenBda_[token & 3u], ESP_BD_ADDR_LEN);
    esp_ble_confirm_reply(bda, accept);
  }

  static void formatPasskeyPrompt(char* out, size_t cap, uint32_t passkey) {
    const unsigned hi = unsigned((passkey / 1000u) % 1000u);
    const unsigned lo = unsigned(passkey % 1000u);
    std::snprintf(out, cap,
                  "Ghép đôi: %03u %03u · GPIO0 đồng ý · GPIO14 từ chối", hi,
                  lo);
  }

  void setSpeech(UiAction& ui, const char* text, uint32_t nowMs) {
    std::snprintf(ui.speech, sizeof(ui.speech), "%s", text);
    ui.showSpeech = true;
    ui.clearSpeech = false;
    lastBubbleRefreshMs_ = nowMs;
  }

  void drainQueues(uint32_t nowMs, UiAction& ui, std::atomic<uint32_t>& session,
                   std::atomic<bool>& appConn, std::atomic<uint16_t>& central) {
    GattsEvent ge;
    while (xQueueReceive(gattsQueue_, &ge, 0) == pdTRUE) {
      if (ge.type == GattsEvent::Type::Connect) {
        portENTER_CRITICAL(&mux_);
        rememberPeerLocked(ge.connId, ge.bda);
        portEXIT_CRITICAL(&mux_);
        if (slots_.findByAddress(ge.bda) >= 0) {
          slots_.upsertAddress(ge.bda, false, currentDay_);
        }
      } else if (ge.type == GattsEvent::Type::AppAccess) {
        if (onAppAccessFromCallback(ge.connId, session, appConn, central)) {
          esp_bd_addr_t bda = {};
          bool found = false;
          portENTER_CRITICAL(&mux_);
          found = bdaForConnIdLocked(ge.connId, bda);
          portEXIT_CRITICAL(&mux_);
          if (found) {
            const int slot = slots_.findByAddress(bda);
            if (slot >= 0 && !slots_.entry(uint8_t(slot)).isAppPhone) {
              slots_.markAppPhone(hid::HidSlotTable::hashAddress(bda),
                                  currentDay_);
              saveSlotsToNvs();
            }
          }
        }
      } else if (ge.type == GattsEvent::Type::HidAccess) {
        applyEncryptedRole(ge.connId, true);
      }
    }

    GapEvent ev;
    while (xQueueReceive(gapQueue_, &ev, 0) == pdTRUE) {
      if (ev.type == GapEvent::Type::NumericConfirm) {
        const bool existingSlot = slots_.findByAddress(ev.bda) >= 0;
        if (!pairingWindow_.allowPrompt(nowMs, slots_.usedCount(), inEgg_,
                                        qrCardOpen_, existingSlot)) {
          esp_ble_confirm_reply(ev.bda, false);
          Serial.println("hid nc_req rejected=window_closed");
          continue;
        }
        const uint8_t token = allocToken(ev.bda);
        const auto r = pairing_.request(ev.passkey, token, nowMs);
        if (r.reply != hid::PairingReply::None) {
          sendConfirmReply(r.replyToken, r.reply == hid::PairingReply::Accept);
        }
        if (r.show) {
          ui.wake = true;
          ui.closeCard = true;
          formatPasskeyPrompt(ui.speech, sizeof(ui.speech), r.passkey);
          ui.showSpeech = true;
          ui.clearSpeech = false;
          lastBubbleRefreshMs_ = nowMs;
          Serial.printf("hid nc_req passkey=%06u\n",
                        unsigned(r.passkey % 1000000u));
        }
      } else if (ev.type == GapEvent::Type::PasskeyNotify) {
        const bool existingSlot = slots_.findByAddress(ev.bda) >= 0;
        if (!pairingWindow_.allowPrompt(nowMs, slots_.usedCount(), inEgg_,
                                        qrCardOpen_, existingSlot)) {
          Serial.println("hid passkey_notif rejected=window_closed");
          continue;
        }
        ui.wake = true;
        ui.closeCard = true;
        std::snprintf(ui.speech, sizeof(ui.speech), "Mã ghép đôi: %03u %03u",
                      unsigned((ev.passkey / 1000u) % 1000u),
                      unsigned(ev.passkey % 1000u));
        ui.showSpeech = true;
        ui.clearSpeech = false;
        lastBubbleRefreshMs_ = nowMs;
        Serial.printf("hid passkey_notif passkey=%06u\n",
                      unsigned(ev.passkey % 1000000u));
      } else if (ev.type == GapEvent::Type::AuthComplete) {
        uint16_t connId = hid::HidLinkRoles::kNoConnId;
        bool isApp = false;
        portENTER_CRITICAL(&mux_);
        connId = connIdForBdaLocked(ev.bda);
        if (connId != hid::HidLinkRoles::kNoConnId) {
          isApp = (roles_.appConnId() == connId);
        }
        portEXIT_CRITICAL(&mux_);
        Serial.printf("hid auth_cmpl conn=%u success=%u reason=0x%02x\n",
                      unsigned(connId), ev.success ? 1u : 0u,
                      unsigned(ev.failReason));
        if (ev.success) {
          if (!slots_.canAcceptBond(ev.bda)) {
            esp_ble_remove_bond_device(ev.bda);
            if (connId != hid::HidLinkRoles::kNoConnId && server_) {
              server_->disconnect(connId);
            }
            Serial.println("hid auth_cmpl rejected=slots_full");
            continue;
          }
          slots_.upsertAddress(ev.bda, isApp, currentDay_);
          saveSlotsToNvs();
          pairingWindow_.onBondSuccess();
        }
        if (connId != hid::HidLinkRoles::kNoConnId) {
          applyEncryptedRole(connId, ev.success);
        }
      }
    }
  }

  void applyEncryptedRole(uint16_t connId, bool encrypted) {
    hid::HidLinkRoles::Action r;
    bool adv = false;
    portENTER_CRITICAL(&mux_);
    r = roles_.onEncrypted(connId, encrypted);
    adv = roles_.shouldAdvertise();
    portEXIT_CRITICAL(&mux_);
    if (r.disconnect && r.disconnectConnId != hid::HidLinkRoles::kNoConnId &&
        server_) {
      server_->disconnect(r.disconnectConnId);
    }
    if (!adv) {
      BLEDevice::getAdvertising()->stop();
    }
  }

  static uint8_t bondCount() {
    const int n = esp_ble_get_bond_device_num();
    return n > 0 ? uint8_t(n > 255 ? 255 : n) : 0;
  }

  int forgetAllBonds() {
    int devNum = esp_ble_get_bond_device_num();
    if (devNum <= 0) return 0;
    if (devNum > 16) devNum = 16;
    esp_ble_bond_dev_t list[16];
    if (esp_ble_get_bond_device_list(&devNum, list) != ESP_OK) return 0;
    int removed = 0;
    for (int i = 0; i < devNum; ++i) {
      if (esp_ble_remove_bond_device(list[i].bd_addr) == ESP_OK) ++removed;
    }
    return removed;
  }

  void serviceSerial(uint32_t nowMs, UiAction& ui) {
    while (Serial.available() > 0) {
      const int ch = Serial.read();
      if (ch < 0) break;
      if (ch == '\r' || ch == '\n') {
        if (cmdLen_ > 0) {
          cmdBuf_[cmdLen_] = 0;
          handleSerialCommand(cmdBuf_, nowMs, ui);
          cmdLen_ = 0;
        }
      } else if (cmdLen_ + 1 < sizeof(cmdBuf_)) {
        cmdBuf_[cmdLen_++] = char(ch);
      }
    }
  }

  void handleSerialCommand(const char* line, uint32_t nowMs, UiAction& ui) {
    while (*line == ' ') ++line;
    if (std::strcmp(line, "hid status") == 0) {
      printStatusLine();
    } else if (std::strcmp(line, "hid forget") == 0) {
      const int removed = forgetAllBonds();
      slots_.forgetAll();
      saveSlotsToNvs();
      Serial.printf("hid forget removed=%d\n", removed);
    } else if (std::strcmp(line, "hid window") == 0) {
      openPairingWindow(nowMs);
      setSpeech(ui, "Đang mở ghép đôi Bluetooth (60s)", nowMs);
      Serial.println("hid window=open");
    } else if (std::strncmp(line, "hid send ", 9) == 0) {
      const int idx = std::atoi(line + 9);
      hid::ConsumerKey key = hid::ConsumerKey::None;
      const bool valid =
          idx >= 1 && idx <= 7 && hid::keyFromIndex(uint8_t(idx), key);
      const bool sent = valid && sendKey(key, nowMs);
      Serial.printf("hid send idx=%d valid=%u sent=%u\n", idx, valid ? 1u : 0u,
                    sent ? 1u : 0u);
    } else if (std::strcmp(line, "hid stop") == 0) {
      const bool sent = sendKey(hid::ConsumerKey::Stop, nowMs);
      setSpeech(ui, "Đã dừng", nowMs);
      Serial.printf("hid stop sent=%u\n", sent ? 1u : 0u);
    }
  }

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  BLEServer* server_ = nullptr;
  BLEHIDDevice* hid_ = nullptr;
  BLECharacteristic* input_ = nullptr;
  BLESecurity security_;
  QueueHandle_t gapQueue_ = nullptr;
  QueueHandle_t gattsQueue_ = nullptr;
  std::atomic<esp_gatt_if_t> gattsIf_{ESP_GATT_IF_NONE};
  uint16_t serviceStartHandle_ = 0;
  uint16_t serviceEndHandle_ = 0;
  uint16_t hidStartHandle_ = 0;
  uint16_t hidEndHandle_ = 0;
  PeerEntry peers_[4] = {};
  esp_bd_addr_t tokenBda_[4] = {};
  uint8_t nextToken_ = 0;

  hid::HidLinkRoles roles_;
  hid::PairingConfirm pairing_;
  hid::PairingWindow pairingWindow_;
  hid::ForgetAllConfirm forgetAllConfirm_;
  hid::HidSlotTable slots_;
  hid::RemoteMode remote_;

  bool inEgg_ = false;
  bool qrCardOpen_ = false;
  uint16_t currentDay_ = 0xFFFF;
  bool pairingWindowPromptPending_ = false;
  bool forgetAllPromptPending_ = false;
  bool deferredReady_ = false;
  uint32_t deferredRequestId_ = 0;
  uint8_t deferredResult_ = 0;

  bool releasePending_ = false;
  uint16_t releaseConnId_ = hid::HidLinkRoles::kNoConnId;
  uint32_t releaseAtMs_ = 0;
  uint32_t lastBubbleRefreshMs_ = 0;
  uint32_t lastDiagMs_ = 0;

  LatencyHistogram<201, 1000> gaps_;
  uint32_t renderMaxUs_ = 0;
  uint32_t bleCmdFail_ = 0;
  uint32_t resets_ = 0;

  char cmdBuf_[64] = {};
  size_t cmdLen_ = 0;
};

}  // namespace xuna
#endif  // defined(XUNA_HID) && XUNA_HID
