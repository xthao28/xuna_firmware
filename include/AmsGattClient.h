#pragma once
#if defined(XUNA_AMS) && XUNA_AMS
#include <Arduino.h>
#include <BLEDevice.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_gap_ble_api.h>
#include <esp_gatt_defs.h>
#include <esp_gattc_api.h>
#include <xuna/hid/AmsClientModel.h>

namespace xuna {

// Experimental Apple Media Service (AMS) GATT client on the existing bonded
// iPhone ACL link (UDC-6 §4.3, env:xuna_ams).
// All Bluedroid GATTC callback events are queued to the main loop task.
class AmsGattClient {
 public:
  static constexpr uint16_t kAmsAppId = 0x58;
  static constexpr uint16_t kNoHandle = 0;
  static constexpr uint16_t kNoConnId = 0xFFFF;

  bool begin() {
    queue_ = xQueueCreate(16, sizeof(GattcEvent));
    if (!queue_) return false;
    model_.reset();
    instancePtr() = this;
    BLEDevice::setCustomGattcHandler(&AmsGattClient::gattcCallback);
    const esp_err_t err = esp_ble_gattc_app_register(kAmsAppId);
    if (err != ESP_OK) {
      Serial.printf("ams app_register err=0x%x\n", unsigned(err));
      return false;
    }
    return true;
  }

  void onPeerEncrypted(const esp_bd_addr_t bda) {
    if (!bda) return;
    std::memcpy(targetBda_, bda, ESP_BD_ADDR_LEN);
    hasTarget_ = true;
    triedPublicAddr_ = false;
    openRequested_ = true;
  }

  void onPeerDisconnected(const esp_bd_addr_t bda) {
    if (hasTarget_ && bda &&
        std::memcmp(targetBda_, bda, ESP_BD_ADDR_LEN) == 0) {
      resetSession();
    }
  }

  // Drains queued GATTC events on the main loop task. Returns true if the
  // track title/artist changed since the last pass so the Remote bubble can
  // refresh immediately.
  bool update(uint32_t /*nowMs*/) {
    const esp_gatt_if_t gattcIf = gattcIf_.load();
    if (openRequested_ && hasTarget_ && gattcIf != ESP_GATT_IF_NONE &&
        connId_ == kNoConnId) {
      openRequested_ = false;
      const esp_ble_addr_type_t addrType =
          triedPublicAddr_ ? BLE_ADDR_TYPE_PUBLIC : BLE_ADDR_TYPE_RANDOM;
      const esp_err_t err =
          esp_ble_gattc_open(gattcIf, targetBda_, addrType, true);
      Serial.printf("ams gattc_open addr_type=%u err=0x%x\n",
                    unsigned(addrType), unsigned(err));
    }

    GattcEvent ev;
    while (queue_ && xQueueReceive(queue_, &ev, 0) == pdTRUE) {
      handleEvent(ev);
    }
    return model_.takeTrackChanged();
  }

  bool ready() const { return model_.ready() && connId_ != kNoConnId; }

  const hid::AmsClientModel& model() const { return model_; }

  bool sendConsumerKey(hid::ConsumerKey key) {
    if (!ready() || remoteCmdHandle_ == kNoHandle) return false;
    const hid::AmsRemoteCommand cmd = hid::AmsClientModel::mapConsumerKey(key);
    if (cmd == hid::AmsRemoteCommand::None ||
        !model_.isCommandSupported(cmd)) {
      return false;
    }
    const esp_gatt_if_t gattcIf = gattcIf_.load();
    if (gattcIf == ESP_GATT_IF_NONE) return false;
    uint8_t cmdByte = static_cast<uint8_t>(cmd);
    const esp_err_t err = esp_ble_gattc_write_char(
        gattcIf, connId_, remoteCmdHandle_, 1, &cmdByte,
        ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    Serial.printf("ams remote_cmd=%u err=0x%x\n", unsigned(cmdByte),
                  unsigned(err));
    return err == ESP_OK;
  }

 private:
  struct GattcEvent {
    enum class Type : uint8_t {
      Open,
      SearchRes,
      SearchCmpl,
      RegNotify,
      WriteChar,
      Notify,
      Close
    } type;
    uint16_t connId;
    uint16_t status;
    uint16_t handle;
    uint16_t endHandle;
    uint8_t dataLen;
    uint8_t data[64];
  };

  static AmsGattClient*& instancePtr() {
    static AmsGattClient* p = nullptr;
    return p;
  }

  static bool matchesUuid128(const esp_bt_uuid_t& uuid,
                             const uint8_t expectedLe[16]) {
    return uuid.len == ESP_UUID_LEN_128 &&
           std::memcmp(uuid.uuid.uuid128, expectedLe, 16) == 0;
  }

  static esp_bt_uuid_t makeUuid128(const uint8_t leBytes[16]) {
    esp_bt_uuid_t u = {};
    u.len = ESP_UUID_LEN_128;
    std::memcpy(u.uuid.uuid128, leBytes, 16);
    return u;
  }

  static esp_bt_uuid_t makeUuid16(uint16_t uuid16) {
    esp_bt_uuid_t u = {};
    u.len = ESP_UUID_LEN_16;
    u.uuid.uuid16 = uuid16;
    return u;
  }

  static void gattcCallback(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                            esp_ble_gattc_cb_param_t* param) {
    AmsGattClient* inst = instancePtr();
    if (!inst || !param) return;
    if (event == ESP_GATTC_REG_EVT) {
      if (param->reg.status == ESP_GATT_OK &&
          param->reg.app_id == kAmsAppId) {
        inst->gattcIf_.store(gattc_if);
      }
      return;
    }
    if (!inst->queue_) return;

    GattcEvent ev = {};
    switch (event) {
      case ESP_GATTC_OPEN_EVT:
        ev.type = GattcEvent::Type::Open;
        ev.connId = param->open.conn_id;
        ev.status = param->open.status;
        xQueueSend(inst->queue_, &ev, 0);
        break;
      case ESP_GATTC_SEARCH_RES_EVT:
        if (matchesUuid128(param->search_res.srvc_id.uuid,
                           hid::AmsClientModel::serviceUuidLe())) {
          ev.type = GattcEvent::Type::SearchRes;
          ev.connId = param->search_res.conn_id;
          ev.handle = param->search_res.start_handle;
          ev.endHandle = param->search_res.end_handle;
          xQueueSend(inst->queue_, &ev, 0);
        }
        break;
      case ESP_GATTC_SEARCH_CMPL_EVT:
        ev.type = GattcEvent::Type::SearchCmpl;
        ev.connId = param->search_cmpl.conn_id;
        ev.status = param->search_cmpl.status;
        xQueueSend(inst->queue_, &ev, 0);
        break;
      case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        ev.type = GattcEvent::Type::RegNotify;
        ev.status = param->reg_for_notify.status;
        ev.handle = param->reg_for_notify.handle;
        xQueueSend(inst->queue_, &ev, 0);
        break;
      case ESP_GATTC_WRITE_CHAR_EVT:
        ev.type = GattcEvent::Type::WriteChar;
        ev.connId = param->write.conn_id;
        ev.status = param->write.status;
        ev.handle = param->write.handle;
        xQueueSend(inst->queue_, &ev, 0);
        break;
      case ESP_GATTC_NOTIFY_EVT:
        ev.type = GattcEvent::Type::Notify;
        ev.connId = param->notify.conn_id;
        ev.handle = param->notify.handle;
        ev.dataLen = param->notify.value_len < sizeof(ev.data)
                         ? uint8_t(param->notify.value_len)
                         : uint8_t(sizeof(ev.data));
        if (param->notify.value && ev.dataLen > 0) {
          std::memcpy(ev.data, param->notify.value, ev.dataLen);
        }
        xQueueSend(inst->queue_, &ev, 0);
        break;
      case ESP_GATTC_CLOSE_EVT:
      case ESP_GATTC_DISCONNECT_EVT:
        ev.type = GattcEvent::Type::Close;
        ev.connId = (event == ESP_GATTC_CLOSE_EVT) ? param->close.conn_id
                                                   : param->disconnect.conn_id;
        xQueueSend(inst->queue_, &ev, 0);
        break;
      default:
        break;
    }
  }

  void handleEvent(const GattcEvent& ev) {
    const esp_gatt_if_t gattcIf = gattcIf_.load();
    if (gattcIf == ESP_GATT_IF_NONE) return;

    switch (ev.type) {
      case GattcEvent::Type::Open:
        if (ev.status != ESP_GATT_OK) {
          Serial.printf("ams open_failed status=0x%x\n", unsigned(ev.status));
          if (!triedPublicAddr_ && hasTarget_) {
            triedPublicAddr_ = true;
            openRequested_ = true;
          }
          return;
        }
        connId_ = ev.connId;
        svcStartHandle_ = kNoHandle;
        svcEndHandle_ = kNoHandle;
        {
          esp_bt_uuid_t svcUuid =
              makeUuid128(hid::AmsClientModel::serviceUuidLe());
          esp_ble_gattc_search_service(gattcIf, connId_, &svcUuid);
        }
        Serial.printf("ams open_ok conn=%u searching=1\n", unsigned(connId_));
        break;

      case GattcEvent::Type::SearchRes:
        svcStartHandle_ = ev.handle;
        svcEndHandle_ = ev.endHandle;
        Serial.printf("ams service_found start=%u end=%u\n",
                      unsigned(svcStartHandle_), unsigned(svcEndHandle_));
        break;

      case GattcEvent::Type::SearchCmpl:
        if (svcStartHandle_ == kNoHandle || svcEndHandle_ == kNoHandle) {
          Serial.println("ams service_not_found");
          return;
        }
        discoverCharacteristics(gattcIf);
        break;

      case GattcEvent::Type::RegNotify:
        if (ev.status != ESP_GATT_OK) {
          Serial.printf("ams reg_notify_failed handle=%u status=0x%x\n",
                        unsigned(ev.handle), unsigned(ev.status));
          return;
        }
        enableCccd(gattcIf, ev.handle);
        if (ev.handle == entityUpdateHandle_) {
          subscribeEntityUpdates(gattcIf);
        }
        break;

      case GattcEvent::Type::WriteChar:
        if (ev.handle == entityUpdateHandle_ && ev.status == ESP_GATT_OK &&
            pendingPlayerSub_) {
          pendingPlayerSub_ = false;
          size_t len = 0;
          const uint8_t* payload =
              hid::AmsClientModel::playerSubscriptionPayload(len);
          esp_ble_gattc_write_char(
              gattcIf, connId_, entityUpdateHandle_, uint16_t(len),
              const_cast<uint8_t*>(payload), ESP_GATT_WRITE_TYPE_RSP,
              ESP_GATT_AUTH_REQ_NONE);
          model_.setReady(true);
          Serial.println("ams ready=1 subscribed=track,player");
        }
        break;

      case GattcEvent::Type::Notify:
        if (ev.handle == remoteCmdHandle_) {
          model_.onRemoteCommandNotify(ev.data, ev.dataLen);
        } else if (ev.handle == entityUpdateHandle_) {
          model_.onEntityUpdateNotify(ev.data, ev.dataLen);
        }
        break;

      case GattcEvent::Type::Close:
        if (ev.connId == connId_) {
          resetSession();
          Serial.println("ams closed=1");
        }
        break;
    }
  }

  void discoverCharacteristics(esp_gatt_if_t gattcIf) {
    remoteCmdHandle_ = findCharHandle(
        gattcIf, hid::AmsClientModel::remoteCommandUuidLe());
    entityUpdateHandle_ = findCharHandle(
        gattcIf, hid::AmsClientModel::entityUpdateUuidLe());
    entityAttrHandle_ = findCharHandle(
        gattcIf, hid::AmsClientModel::entityAttributeUuidLe());

    Serial.printf("ams chars remote_cmd=%u entity_update=%u entity_attr=%u\n",
                  unsigned(remoteCmdHandle_), unsigned(entityUpdateHandle_),
                  unsigned(entityAttrHandle_));

    if (remoteCmdHandle_ != kNoHandle) {
      esp_ble_gattc_register_for_notify(gattcIf, targetBda_, remoteCmdHandle_);
    }
    if (entityUpdateHandle_ != kNoHandle) {
      esp_ble_gattc_register_for_notify(gattcIf, targetBda_,
                                        entityUpdateHandle_);
    }
  }

  uint16_t findCharHandle(esp_gatt_if_t gattcIf, const uint8_t uuidLe[16]) {
    uint16_t count = 1;
    esp_gattc_char_elem_t elem = {};
    esp_bt_uuid_t charUuid = makeUuid128(uuidLe);
    const esp_gatt_status_t st = esp_ble_gattc_get_char_by_uuid(
        gattcIf, connId_, svcStartHandle_, svcEndHandle_, charUuid, &elem,
        &count);
    if (st == ESP_GATT_OK && count > 0) {
      return elem.char_handle;
    }
    return kNoHandle;
  }

  void enableCccd(esp_gatt_if_t gattcIf, uint16_t charHandle) {
    uint16_t count = 1;
    esp_gattc_descr_elem_t descr = {};
    esp_bt_uuid_t cccdUuid = makeUuid16(ESP_GATT_UUID_CHAR_CLIENT_CONFIG);
    const esp_gatt_status_t st = esp_ble_gattc_get_descr_by_char_handle(
        gattcIf, connId_, charHandle, cccdUuid, &descr, &count);
    if (st == ESP_GATT_OK && count > 0) {
      uint8_t notifyOn[2] = {0x01, 0x00};
      esp_ble_gattc_write_char_descr(
          gattcIf, connId_, descr.handle, sizeof(notifyOn), notifyOn,
          ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    }
  }

  void subscribeEntityUpdates(esp_gatt_if_t gattcIf) {
    if (entityUpdateHandle_ == kNoHandle) return;
    size_t len = 0;
    const uint8_t* trackPayload =
        hid::AmsClientModel::trackSubscriptionPayload(len);
    pendingPlayerSub_ = true;
    esp_ble_gattc_write_char(
        gattcIf, connId_, entityUpdateHandle_, uint16_t(len),
        const_cast<uint8_t*>(trackPayload), ESP_GATT_WRITE_TYPE_RSP,
        ESP_GATT_AUTH_REQ_NONE);
  }

  void resetSession() {
    connId_ = kNoConnId;
    svcStartHandle_ = kNoHandle;
    svcEndHandle_ = kNoHandle;
    remoteCmdHandle_ = kNoHandle;
    entityUpdateHandle_ = kNoHandle;
    entityAttrHandle_ = kNoHandle;
    pendingPlayerSub_ = false;
    hasTarget_ = false;
    openRequested_ = false;
    triedPublicAddr_ = false;
    model_.reset();
  }

  QueueHandle_t queue_ = nullptr;
  std::atomic<esp_gatt_if_t> gattcIf_{ESP_GATT_IF_NONE};
  esp_bd_addr_t targetBda_ = {};
  bool hasTarget_ = false;
  bool openRequested_ = false;
  bool triedPublicAddr_ = false;
  bool pendingPlayerSub_ = false;
  uint16_t connId_ = kNoConnId;
  uint16_t svcStartHandle_ = kNoHandle;
  uint16_t svcEndHandle_ = kNoHandle;
  uint16_t remoteCmdHandle_ = kNoHandle;
  uint16_t entityUpdateHandle_ = kNoHandle;
  uint16_t entityAttrHandle_ = kNoHandle;
  hid::AmsClientModel model_;
};

}  // namespace xuna
#endif  // defined(XUNA_AMS) && XUNA_AMS

