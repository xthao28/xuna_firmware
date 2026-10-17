#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <atomic>
#include <cstring>
#include <xuna/AnimationManager.h>
#include <xuna/BleProtocol.h>
#include <xuna/identity/IdentityCodec.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>
namespace xuna {
class BleManager {
  static constexpr const char* kService="7a6e0001-8c44-4db6-9f61-2b7d5300a101";
  struct Packet { uint8_t bytes[16];size_t size;uint32_t session; };
  QueueHandle_t queue_=nullptr;
  BLECharacteristic *status_=nullptr,*lifeStatus_=nullptr;
  BLECharacteristic* explanation_=nullptr;
  BLECharacteristic* identity_=nullptr;
  BLECharacteristic* memories_[2]={nullptr,nullptr};
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> session_{0};
  uint32_t seenSession_=0,sequence_=0;
  uint8_t lastState_=255;
  bool advertising_=true;
  uint8_t lastLife_[20]={},lastMemories_[2][20]={},lastExplanation_[20]={},lastIdentity_[IdentityCodec::kSize]={};
  std::atomic<uint16_t> central_{0xffff};
  class ServerCallbacks: public BLEServerCallbacks {
    BleManager& owner_;
   public:
    explicit ServerCallbacks(BleManager& o):owner_(o){}
    void onConnect(BLEServer* server,esp_ble_gatts_cb_param_t* param) override {
      if(owner_.connected_){server->disconnect(param->connect.conn_id);return;}
      owner_.central_=param->connect.conn_id;++owner_.session_;owner_.connected_=true;
    }
    void onDisconnect(BLEServer*,esp_ble_gatts_cb_param_t* param) override {
      if(owner_.central_!=param->disconnect.conn_id)return;
      owner_.central_=0xffff;owner_.connected_=false;++owner_.session_;
    }
  } serverCallbacks_{*this};
  class WriteCallbacks: public BLECharacteristicCallbacks {
    BleManager& owner_;
   public:
    explicit WriteCallbacks(BleManager& o):owner_(o){}
    void onWrite(BLECharacteristic* characteristic) override {
      const auto value=characteristic->getValue();
      Packet p={};p.size=value.size();p.session=owner_.session_.load();
      for(size_t i=0;i<value.size()&&i<sizeof(p.bytes);++i)p.bytes[i]=value[i];
      // Overflow drops the request: client timeout must not auto-replay.
      xQueueSend(owner_.queue_,&p,0);
    }
  } writeCallbacks_{*this};
  void publish(uint8_t kind,uint32_t id,uint8_t result,PetState state,uint32_t now) {
    uint8_t b[16];
    V1LifeCoreAdapter::encodeStatus(kind,id,result,static_cast<uint8_t>(state),sequence_,now,b);
    status_->setValue(b,sizeof(b));
    if(connected_)status_->notify();
  }
 public:
  bool begin() {
    queue_=xQueueCreate(8,sizeof(Packet));if(!queue_)return false;
    BLEDevice::init("XUNA");auto* server=BLEDevice::createServer();server->setCallbacks(&serverCallbacks_);
    // Default createService budget is 15 handles; this service needs 18 (service,
    // 8 characteristics × 2, status CCCD). Too few silently drops 000b/000c.
    auto* service=server->createService(BLEUUID(kService),32);
    auto* caps=service->createCharacteristic("7a6e0002-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    uint8_t b[8]={1,8,0xbf,0,15,3,0x60,0x09};caps->setValue(b,sizeof(b));
    auto* command=service->createCharacteristic("7a6e0003-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_WRITE);command->setCallbacks(&writeCallbacks_);
    status_=service->createCharacteristic("7a6e0004-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY);status_->addDescriptor(new BLE2902());
    lifeStatus_=service->createCharacteristic("7a6e0005-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    memories_[0]=service->createCharacteristic("7a6e0006-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    memories_[1]=service->createCharacteristic("7a6e0007-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    explanation_=service->createCharacteristic("7a6e000b-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    identity_=service->createCharacteristic("7a6e000c-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    publish(0,0,0,PetState::Idle,0);service->start();
    auto* adv=BLEDevice::getAdvertising();adv->addServiceUUID(kService);adv->setScanResponse(true);adv->start();return true;
  }
  // All life/learning decisions belong to the pure adapter; callbacks only queue bytes.
  bool update(V1LifeCoreAdapter& adapter,const IdentityRecord* identity,uint32_t now,bool ready) {
    const auto session=session_.load();
    if(session!=seenSession_){adapter.onSession();seenSession_=session;}
    if(connected_)advertising_=false;
    else if(!advertising_){BLEDevice::startAdvertising();advertising_=true;}
    Packet p;bool interacted=false,reply=false;
    uint32_t replyId=0;uint8_t replyResult=0;PetState replyState=PetState::Idle;
    if(xQueueReceive(queue_,&p,0)==pdTRUE&&p.session==session&&connected_){
      uint32_t id=0;
      const uint8_t result=adapter.handle(p.bytes,p.size,now,ready,id);
      AnimationManager presentation;
      presentation.setIntent(adapter.visual(),0,now);
      reply=true;replyId=id;replyResult=result;replyState=presentation.state();
      interacted=adapter.last().actionAccepted;
      if(p.size>=2&&p.bytes[1]==3)
        Serial.printf("stack tag=feedback high_water_bytes=%u result=%u\n",
                      unsigned(uxTaskGetStackHighWaterMark(nullptr)),result);
      if(p.size>=2&&p.bytes[1]==5){
        const auto quality=adapter.clock().quality(adapter.monoNow());
        const auto part=adapter.clock().dayPart(adapter.monoNow());
        Serial.printf("time_sync result=%u quality=%u day_part=%u tz_min=%d\n",
                      result,unsigned(quality),unsigned(part),
                      int(adapter.clock().tzOffsetMin()));
      }
    }
    uint8_t b[20];adapter.encodeLife(b);
    if(std::memcmp(lastLife_,b,sizeof(b))){std::memcpy(lastLife_,b,sizeof(b));lifeStatus_->setValue(b,sizeof(b));}
    for(uint8_t page=0;page<2;++page){
      adapter.encodeMemory(page,b);
      if(std::memcmp(lastMemories_[page],b,sizeof(b))){std::memcpy(lastMemories_[page],b,sizeof(b));memories_[page]->setValue(b,sizeof(b));}
    }
    adapter.encodeExplanation(b);
    if(std::memcmp(lastExplanation_,b,sizeof(b))){std::memcpy(lastExplanation_,b,sizeof(b));explanation_->setValue(b,sizeof(b));}
    uint8_t id[IdentityCodec::kSize];IdentityCodec::encode(identity,adapter.generation(),id);
    if(std::memcmp(lastIdentity_,id,sizeof(id))){std::memcpy(lastIdentity_,id,sizeof(id));identity_->setValue(id,sizeof(id));}
    // Publish only after all read projections reflect the committed command.
    if(reply)publish(1,replyId,replyResult,replyState,now);
    return interacted;
  }
  void stateChanged(PetState state,uint32_t now) {
    const auto s=static_cast<uint8_t>(state);if(s!=lastState_){lastState_=s;++sequence_;publish(2,0,0,state,now);}
  }
};
}
