#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <atomic>
#include <cstring>
#include <xuna/AnimationManager.h>
#include <xuna/BleProtocol.h>
#include <xuna/life/ActionGate.h>
#include "LifeStorage.h"
namespace xuna {
class BleManager {
  static constexpr const char* kService="7a6e0001-8c44-4db6-9f61-2b7d5300a101";
  struct Packet { uint8_t bytes[12];size_t size;uint32_t session; };
  QueueHandle_t queue_=nullptr;
  BLECharacteristic *status_=nullptr,*lifeStatus_=nullptr;
  BLECharacteristic* memories_[2]={nullptr,nullptr};
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> session_{0};
  uint32_t seenSession_=0,sequence_=0,decision_=0,activityAt_=0;
  uint8_t context_=0,action_=0,lastState_=255;
  ActionGate gate_;
  bool feedbackUsed_=true,advertising_=true;
  CommandCache cache_;
  uint8_t lastLife_[20]={},lastMemories_[2][20]={};
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
    uint8_t b[16]={1,kind};LifeEngine::put32(b+2,id);b[6]=result;b[7]=static_cast<uint8_t>(state);
    LifeEngine::put32(b+8,sequence_);LifeEngine::put32(b+12,now);status_->setValue(b,sizeof(b));
    if(connected_)status_->notify();
  }
  static uint8_t validationResult(ValidationReason reason) {
    switch(reason) {
      case ValidationReason::Accepted: return 0;
      case ValidationReason::Busy:
      case ValidationReason::Activity:
      case ValidationReason::Cooldown:
      case ValidationReason::Energy:
      case ValidationReason::Permission:
      case ValidationReason::Stage: return 5;
      case ValidationReason::UnknownAction:
      case ValidationReason::InvalidParameter:
      case ValidationReason::InvalidContext: return 4;
    }
    return 4;
  }
  bool requestAction(uint8_t id,AnimationManager& animation,ActivityFsm& activity,
                     Needs& needs,uint32_t now,uint8_t& result) {
    const ActionOutcome outcome=gate_.request(id,0,activity,needs.state().energy,now);
    // A validator pass refused by an FSM guard is reported as busy (5).
    if(!outcome.accepted){result=outcome.reason==ValidationReason::Accepted?5:validationResult(outcome.reason);return false;}
    animation.setIntent(outcome.visual,0,now);
    result=0;return true;
  }
 public:
  bool begin() {
    queue_=xQueueCreate(8,sizeof(Packet));if(!queue_)return false;
    BLEDevice::init("XUNA");auto* server=BLEDevice::createServer();server->setCallbacks(&serverCallbacks_);
    auto* service=server->createService(kService);
    auto* caps=service->createCharacteristic("7a6e0002-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    uint8_t b[8]={1,8,15,0,15,3,0x60,0x09};caps->setValue(b,sizeof(b));
    auto* command=service->createCharacteristic("7a6e0003-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_WRITE);command->setCallbacks(&writeCallbacks_);
    status_=service->createCharacteristic("7a6e0004-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY);status_->addDescriptor(new BLE2902());
    lifeStatus_=service->createCharacteristic("7a6e0005-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    memories_[0]=service->createCharacteristic("7a6e0006-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    memories_[1]=service->createCharacteristic("7a6e0007-8c44-4db6-9f61-2b7d5300a101",BLECharacteristic::PROPERTY_READ);
    publish(0,0,0,PetState::Idle,0);service->start();
    auto* adv=BLEDevice::getAdvertising();adv->addServiceUUID(kService);adv->setScanResponse(true);adv->start();return true;
  }
  bool requestLocalAction(AnimationManager& animation,ActivityFsm& activity,Needs& needs,
                          ActionId action,uint32_t now) {
    uint8_t result=0;
    const bool accepted=requestAction(static_cast<uint8_t>(action),animation,activity,needs,now,result);
    if(accepted) { invalidateDecision(); activityAt_=now; }
    return accepted;
  }
  void update(AnimationManager& animation,LifeEngine& life,LifeStorage& storage,
              ActivityFsm& activity,Needs& needs,uint32_t now,bool ready) {
    const auto session=session_.load();
    if(session!=seenSession_){cache_.clear();decision_=0;feedbackUsed_=true;seenSession_=session;}
    if(connected_)advertising_=false;
    else if(!advertising_){BLEDevice::startAdvertising();advertising_=true;}
    Packet p;bool reply=false;uint32_t replyId=0;uint8_t replyResult=0;
    if(xQueueReceive(queue_,&p,0)==pdTRUE&&p.session==session&&connected_){
      auto c=parseCommand(p.bytes,p.size);uint8_t result=c.error;
      const int duplicate=p.size<=sizeof(p.bytes)?cache_.find(p.bytes,p.size):-1;
      if(duplicate>=0)result=duplicate;
      else {
        if(!result&&!ready)result=7;
        if(!result){
          if(c.opcode==1){
            if(requestAction(c.arg,animation,activity,needs,now,result)) { invalidateDecision(); activityAt_=now; }
          }
          if(c.opcode==2){
            const uint8_t selected=life.choose(c.arg);
            // Commit context with the decision only: a rejected choice must not
            // retarget feedback for the previous accepted decision.
            if(requestAction(selected,animation,activity,needs,now,result)) {
              context_=c.arg; action_=selected; decision_=c.id; feedbackUsed_=false; activityAt_=now;
            }
          }
          if(c.opcode==3){
            if(c.target!=decision_||feedbackUsed_)result=4;
            else {LifeEngine candidate=life;candidate.feedback(context_,action_,c.arg==1,c.source);if(storage.save(candidate)){life=candidate;feedbackUsed_=true;}else result=7;}
          }
          if(c.opcode==4){if(!storage.clear(life))result=7;decision_=0;feedbackUsed_=true;}
        }
        if(c.id)cache_.remember(p.bytes,p.size,result);
      }
      reply=true;replyId=c.id;replyResult=result;
    }
    uint8_t b[20]={1,20,context_,action_};LifeEngine::put32(b+4,life.activeSeconds);LifeEngine::put32(b+8,life.eventCount);LifeEngine::put32(b+12,decision_);
    b[16]=life.choose(0);b[17]=life.choose(1);b[18]=feedbackUsed_?0:1;b[19]=life.episodeCount;if(std::memcmp(lastLife_,b,sizeof(b))){std::memcpy(lastLife_,b,sizeof(b));lifeStatus_->setValue(b,sizeof(b));}
    for(int page=0;page<2;++page){
      uint8_t memory[20]={1,0,static_cast<uint8_t>(page),0};
      for(int i=0;i<4;++i){const int index=page*4+i;if(index>=life.episodeCount)break;
        const int oldest=life.episodeCount==8?life.episodeNext:0;
        const auto& e=life.episodes[(oldest+index)%8];
        memory[4+i*4]=e.context;memory[5+i*4]=e.action;memory[6+i*4]=e.approved;memory[7+i*4]=e.source;++memory[1];
      }if(std::memcmp(lastMemories_[page],memory,sizeof(memory))){std::memcpy(lastMemories_[page],memory,sizeof(memory));memories_[page]->setValue(memory,sizeof(memory));}
    }
    if(reply)publish(1,replyId,replyResult,animation.state(),now);
  }
  uint32_t activityAt() const {return activityAt_;}
  void invalidateDecision(){decision_=0;feedbackUsed_=true;}
  void stateChanged(PetState state,uint32_t now) {
    const auto s=static_cast<uint8_t>(state);if(s!=lastState_){lastState_=s;++sequence_;publish(2,0,0,state,now);}
  }
};
}
