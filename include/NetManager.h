#pragma once
// Device-direct network task (DC-2, built with XUNA_NET=1). Runs on core 0
// next to the Wi-Fi/BT/lwIP tasks; the life loop on core 1 only hands over
// settings through a queue and copies a status snapshot. A dead router or
// server changes nothing but this status (offline-first, H1).
#include <Arduino.h>
#include <WiFi.h>
#include <xuna/net/NetConfig.h>
namespace xuna {
class NetManager {
 public:
  bool begin(const NetConfig* stored) {
    queue_=xQueueCreate(1,sizeof(Command));
    if(!queue_)return false;
    if(stored&&stored->valid())apply(*stored);
    return xTaskCreatePinnedToCore(&NetManager::run,"xuna_net",kStackBytes,this,kPriority,&task_,0)==pdPASS;
  }
  // New settings or forget; the newest wins if the task has not taken the last one.
  void apply(const NetConfig& c){Command cmd;cmd.op=NetOp::Set;cmd.config=c;xQueueOverwrite(queue_,&cmd);cmd.config.wipe();}
  void forget(){Command cmd;cmd.op=NetOp::Forget;xQueueOverwrite(queue_,&cmd);}
  NetStatus status() const {portENTER_CRITICAL(&lock_);const NetStatus s=status_;portEXIT_CRITICAL(&lock_);return s;}

 protected:
  static constexpr uint32_t kStackBytes=8192;  // TLS handshake in DC-4
  static constexpr UBaseType_t kPriority=1;     // below Wi-Fi, BT and lwIP on core 0
  static constexpr uint32_t kJoinTimeoutMs=20000,kRetryMs=30000,kRejoinMs=2000;
  struct Command { NetOp op=NetOp::None;NetConfig config; };
  QueueHandle_t queue_=nullptr;
  TaskHandle_t task_=nullptr;
  mutable portMUX_TYPE lock_=portMUX_INITIALIZER_UNLOCKED;
  NetStatus status_;
  NetConfig config_;  // task-owned copy
  template<class F> void edit(F f){portENTER_CRITICAL(&lock_);f(status_);portEXIT_CRITICAL(&lock_);}
  void setWifi(WifiState w,NetError e,int8_t rssi){edit([&](NetStatus& s){s.wifi=w;s.lastError=e;s.rssi=rssi;});}
  static void run(void* self){static_cast<NetManager*>(self)->loop();}
  // Takes a pending command; true when the settings changed.
  bool takeCommand(uint32_t waitMs) {
    Command cmd;
    if(xQueueReceive(queue_,&cmd,pdMS_TO_TICKS(waitMs))!=pdTRUE)return false;
    config_.wipe();
    if(cmd.op==NetOp::Set)config_=cmd.config;
    cmd.config.wipe();
    return true;
  }
  void radioOff() {
    WiFi.disconnect(true,true);WiFi.mode(WIFI_OFF);
    setWifi(WifiState::NotConfigured,NetError::None,0);
    Serial.println("net event=wifi_off");
  }
  // Joins config_; false on failure (status says why).
  bool join() {
    WiFi.persistent(false);  // credentials stay in xuna_net only
    WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);
    WiFi.begin(config_.ssid,config_.pass[0]?config_.pass:nullptr);
    setWifi(WifiState::Joining,NetError::None,0);
    Serial.println("net event=wifi_joining");
    const uint32_t started=millis();
    for(;;){
      const wl_status_t st=WiFi.status();
      if(st==WL_CONNECTED){
        const int8_t rssi=int8_t(WiFi.RSSI());setWifi(WifiState::Connected,NetError::None,rssi);
        Serial.printf("net event=wifi_up ms=%u rssi=%d\n",unsigned(millis()-started),int(rssi));
        return true;
      }
      const bool timeout=uint32_t(millis()-started)>=kJoinTimeoutMs;
      if(st==WL_NO_SSID_AVAIL||st==WL_CONNECT_FAILED||timeout){
        const NetError e=st==WL_NO_SSID_AVAIL?NetError::WifiNotFound:st==WL_CONNECT_FAILED?NetError::WifiAuth:NetError::WifiOther;
        setWifi(WifiState::Failed,e,0);WiFi.disconnect(false,false);
        Serial.printf("net event=wifi_failed error=%u\n",unsigned(e));
        return false;
      }
      if(uxQueueMessagesWaiting(queue_))return false;  // new settings: start over
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }
  // While joined: watch the link; DC-4 runs the cloud session here.
  void online() {
    while(WiFi.status()==WL_CONNECTED&&!uxQueueMessagesWaiting(queue_)){
      const int8_t rssi=int8_t(WiFi.RSSI());
      edit([&](NetStatus& s){s.rssi=rssi;});
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  void loop() {
    for(;;){
      if(!config_.valid()){
        if(status_.wifi!=WifiState::NotConfigured||WiFi.getMode()!=WIFI_OFF)radioOff();
        takeCommand(portMAX_DELAY);
        continue;
      }
      uint32_t waitMs=kRetryMs;  // a failed join (wrong password, no network) waits longer
      if(join()){
        online();
        if(!uxQueueMessagesWaiting(queue_)){
          setWifi(WifiState::Joining,NetError::None,0);
          Serial.println("net event=wifi_lost");
        }
        waitMs=kRejoinMs;
      }
      // Wait before retrying, but react at once to new settings.
      takeCommand(uxQueueMessagesWaiting(queue_)?0:waitMs);
    }
  }
};
}
