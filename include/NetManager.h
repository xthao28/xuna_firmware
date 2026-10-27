#pragma once
// Device-direct network task (DC-2/DC-4, built with XUNA_NET=1). Runs on
// core 0 next to the Wi-Fi/BT/lwIP tasks; the life loop on core 1 only hands
// over settings and identity, signs one session proof per connection on
// request, and copies a status snapshot. A dead router or server changes
// nothing but this status (offline-first, H1). Up only: nothing the server
// sends changes device state (H2).
//
// Transport: esp_transport_ws over esp_transport_ssl from this task (not
// esp_websocket_client, whose IDF 4.4 task is unpinned; ADR-0010), TLS
// pinned to the LAN dev CA (DC-D8). Signing stays on the life loop because
// DeviceKey (mbedTLS context, CPU boost) is not shared between tasks.
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_tls_errors.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <xuna/base/Sha256.h>
#include <xuna/identity/DeviceProof.h>
#include <xuna/net/CloudSession.h>
#include <xuna/net/NetConfig.h>
#if !__has_include("E14DevCa.h")
#error "XUNA_NET needs the pinned CA include/E14DevCa.h: run backend/scripts/make_dev_ca.py --firmware-header (docs/roadmap/cloud/E14-runbook.md)"
#endif
#include "E14DevCa.h"
namespace xuna {
class NetManager {
 public:
  bool begin(const NetConfig* stored,const char* firmware) {
    queue_=xQueueCreate(1,sizeof(Command));
    signJobs_=xQueueCreate(1,sizeof(SignJob));
    signResults_=xQueueCreate(1,sizeof(SignResult));
    if(!queue_||!signJobs_||!signResults_)return false;
    std::snprintf(firmware_,sizeof(firmware_),"%s",firmware);
    cloud_.configure("",firmware_);
    if(stored&&stored->valid())apply(*stored);
    return xTaskCreatePinnedToCore(&NetManager::run,"xuna_net",kStackBytes,this,kPriority,&task_,0)==pdPASS;
  }
  // New settings or forget; the newest wins if the task has not taken the last one.
  void apply(const NetConfig& c){Command cmd;cmd.op=NetOp::Set;cmd.config=c;xQueueOverwrite(queue_,&cmd);cmd.config.wipe();}
  void forget(){Command cmd;cmd.op=NetOp::Forget;xQueueOverwrite(queue_,&cmd);}
  // The pet's PetId once it has one (an Egg stays offline until it hatches).
  void identify(const uint8_t petId[16]) {
    char hex[33];for(int i=0;i<16;++i)std::snprintf(hex+2*i,3,"%02x",petId[i]);
    portENTER_CRITICAL(&lock_);std::memcpy(pendingPet_,hex,sizeof(hex));portEXIT_CRITICAL(&lock_);
  }
  NetStatus status() const {portENTER_CRITICAL(&lock_);const NetStatus s=status_;portEXIT_CRITICAL(&lock_);return s;}
  // Life loop: signs a pending session proof (purpose 3, payload = own key)
  // with the current generation. At most once per connection attempt.
  void serviceSigning(IDeviceSigner& signer,const uint8_t* petId,uint32_t generation) {
    SignJob job;
    if(xQueueReceive(signJobs_,&job,0)!=pdTRUE)return;
    SignResult result;result.generation=generation;
    if(petId&&signer.ready()){
      uint8_t message[DeviceProof::kMessageSize],digest[Sha256::kSize];
      DeviceProof::message(job.nonce,petId,ProofPurpose::Session,generation,signer.publicKey(),
                           DeviceProof::kPublicKeySize,message);
      Sha256::hash(message,sizeof(message),digest);
      const uint32_t started=micros();
      result.ok=signer.sign(digest,result.signature);
      Serial.printf("cloud_proof ok=%u sign_us=%u\n",result.ok?1u:0u,unsigned(micros()-started));
    }
    xQueueOverwrite(signResults_,&result);
  }

 private:
  static constexpr uint32_t kStackBytes=8192;  // TLS handshake + cert verify
  static constexpr UBaseType_t kPriority=1;     // below Wi-Fi, BT and lwIP on core 0
  static constexpr uint32_t kJoinTimeoutMs=20000,kRetryMs=30000,kRejoinMs=2000;
  static constexpr int kConnectTimeoutMs=10000,kIoTimeoutMs=2000,kPollMs=200;
  static constexpr uint32_t kSignWaitMs=5000,kHandshakeMs=75000;
  struct Command { NetOp op=NetOp::None;NetConfig config; };
  struct SignJob { uint8_t nonce[32]; };
  struct SignResult { bool ok=false;uint32_t generation=0;uint8_t signature[64]={}; };
  QueueHandle_t queue_=nullptr,signJobs_=nullptr,signResults_=nullptr;
  TaskHandle_t task_=nullptr;
  mutable portMUX_TYPE lock_=portMUX_INITIALIZER_UNLOCKED;
  NetStatus status_;
  char pendingPet_[33]={};
  char firmware_[33]={};
  NetConfig config_;  // task-owned copy
  CloudSession cloud_;
  esp_transport_handle_t ssl_=nullptr,ws_=nullptr;
  char tx_[CloudSession::kMaxFrame],rx_[CloudSession::kMaxFrame];
  uint32_t nextAttemptAt_=0;
  template<class F> void edit(F f){portENTER_CRITICAL(&lock_);f(status_);portEXIT_CRITICAL(&lock_);}
  void setWifi(WifiState w,NetError e,int8_t rssi){edit([&](NetStatus& s){s.wifi=w;s.lastError=e;s.rssi=rssi;});}
  void setCloud(CloudState c,NetError e){edit([&](NetStatus& s){s.cloud=c;s.lastError=e;});}
  static void run(void* self){static_cast<NetManager*>(self)->loop();}
  bool commandWaiting() const {return uxQueueMessagesWaiting(queue_)!=0;}
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
      if(commandWaiting())return false;  // new settings: start over
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }
  void takeIdentity() {
    char pet[33];
    portENTER_CRITICAL(&lock_);std::memcpy(pet,pendingPet_,sizeof(pet));portEXIT_CRITICAL(&lock_);
    if(pet[0]&&!cloud_.configured())cloud_.configure(pet,firmware_);
  }
  bool sendText(const char* text,size_t n) {
    return esp_transport_ws_send_raw(ws_,ws_transport_opcodes_t(WS_TRANSPORT_OPCODES_TEXT|WS_TRANSPORT_OPCODES_FIN),
                                     text,int(n),kIoTimeoutMs)>=0;
  }
  // One whole frame into rx_ (NUL-terminated); 0 = nothing, -1 = gone.
  // opcode/closeCode describe it; server frames are small, larger is an error.
  int readFrame(int& opcode) {
    const int n=esp_transport_read(ws_,rx_,sizeof(rx_)-1,kPollMs);
    if(n<=0)return n;
    const int total=esp_transport_ws_get_read_payload_len(ws_);
    if(total>int(sizeof(rx_))-1)return -1;
    int got=n;
    while(got<total){
      const int m=esp_transport_read(ws_,rx_+got,int(sizeof(rx_))-1-got,kIoTimeoutMs);
      if(m<=0)return -1;
      got+=m;
    }
    rx_[got]=0;opcode=esp_transport_ws_get_read_opcode(ws_)&0x0f;return got;
  }
  void ensureTransport() {
    if(ws_)return;
    ssl_=esp_transport_ssl_init();
    esp_transport_ssl_set_cert_data(ssl_,kE14CaPem,int(std::strlen(kE14CaPem)));
    ws_=esp_transport_ws_init(ssl_);
    esp_transport_ws_config_t config={};
    config.ws_path="/v1/devices/connect";config.sub_protocol="xuna.device.v1";config.propagate_control_frames=true;
    esp_transport_ws_set_config(ws_,&config);
  }
  // Waits for the life loop's signature; false on timeout or failure.
  bool awaitSignature(SignResult& out) {
    SignJob job;std::memcpy(job.nonce,cloud_.nonce(),sizeof(job.nonce));
    xQueueReset(signResults_);xQueueOverwrite(signJobs_,&job);
    const bool got=xQueueReceive(signResults_,&out,pdMS_TO_TICKS(kSignWaitMs))==pdTRUE;
    return got&&out.ok;
  }
  // The session ends: close, back off, report.
  void end(NetError error) {
    esp_transport_close(ws_);
    const uint32_t waitMs=cloud_.ended(error,esp_random());
    nextAttemptAt_=millis()+waitMs;
    setCloud(cloud_.state(),error);
    Serial.printf("net event=cloud_closed error=%u wait_ms=%u\n",unsigned(error),unsigned(waitMs));
  }
  // One connection attempt, until it ends or the settings change.
  void session() {
    cloud_.connecting();setCloud(CloudState::Connecting,status_.lastError);
    ensureTransport();
    const uint32_t started=millis();
    if(esp_transport_connect(ws_,config_.host,config_.port,kConnectTimeoutMs)<0){
      const esp_tls_error_handle_t tls=esp_transport_get_error_handle(ws_);
      const bool tlsFailed=tls&&(tls->esp_tls_flags||tls->esp_tls_error_code);
      Serial.printf("net event=cloud_connect_failed ms=%u errno=%d tls=-0x%04x flags=0x%x\n",
                    unsigned(millis()-started),esp_transport_get_errno(ws_),unsigned(tls?-tls->esp_tls_error_code:0),
                    unsigned(tls?tls->esp_tls_flags:0));
      end(tlsFailed?NetError::Tls:NetError::ServerUnreachable);
      return;
    }
    Serial.printf("net event=cloud_open ms=%u heap_int_free=%u\n",unsigned(millis()-started),
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    const size_t hello=cloud_.opened(tx_,sizeof(tx_),millis());
    if(!hello||!sendText(tx_,hello)){end(NetError::ServerUnreachable);return;}
    setCloud(CloudState::Proving,status_.lastError);
    const uint32_t openedAt=millis();
    for(;;){
      if(WiFi.status()!=WL_CONNECTED){end(NetError::WifiOther);return;}
      if(commandWaiting()){esp_transport_close(ws_);cloud_.stop();setCloud(CloudState::Off,NetError::None);return;}
      if(cloud_.state()!=CloudState::Ready&&uint32_t(millis()-openedAt)>kHandshakeMs){end(NetError::Protocol);return;}
      int opcode=0;
      const int n=readFrame(opcode);
      if(n<0){end(NetError::ServerUnreachable);return;}
      if(n>0&&opcode==WS_TRANSPORT_OPCODES_CLOSE){
        const uint16_t code=n>=2?uint16_t((uint8_t(rx_[0])<<8)|uint8_t(rx_[1])):1005;
        end(CloudSession::errorFor(code,n>2?rx_+2:""));return;
      }
      if(n>0&&opcode==WS_TRANSPORT_OPCODES_PING){
        esp_transport_ws_send_raw(ws_,ws_transport_opcodes_t(WS_TRANSPORT_OPCODES_PONG|WS_TRANSPORT_OPCODES_FIN),
                                  rx_,n,kIoTimeoutMs);
      }
      size_t outLen=0;
      CloudSession::Action action=CloudSession::Action::None;
      if(n>0&&opcode==WS_TRANSPORT_OPCODES_TEXT)action=cloud_.received(rx_,size_t(n),tx_,sizeof(tx_),outLen,millis());
      else if(n>0&&opcode==WS_TRANSPORT_OPCODES_BINARY)action=CloudSession::Action::Fail;
      else action=cloud_.tick(millis(),tx_,sizeof(tx_),outLen);
      switch(action){
        case CloudSession::Action::Sign: {
          SignResult signature;
          if(!awaitSignature(signature)){end(NetError::Unauthorized);return;}
          const size_t proof=cloud_.signedProof(signature.generation,signature.signature,tx_,sizeof(tx_));
          std::memset(&signature,0,sizeof(signature));
          if(!proof||!sendText(tx_,proof)){end(NetError::ServerUnreachable);return;}
          break;
        }
        case CloudSession::Action::Ready:
          setCloud(CloudState::Ready,NetError::None);
          Serial.printf("net event=cloud_ready ms=%u heartbeat_s=%u\n",unsigned(millis()-started),
                        unsigned(cloud_.heartbeatMs()/1000));
          break;
        case CloudSession::Action::Send:
          if(!sendText(tx_,outLen)){end(NetError::ServerUnreachable);return;}
          break;
        case CloudSession::Action::Fail:
          end(cloud_.lastError()==NetError::None?NetError::Protocol:cloud_.lastError());return;
        case CloudSession::Action::None:break;
      }
      const int8_t rssi=int8_t(WiFi.RSSI());
      edit([&](NetStatus& s){s.rssi=rssi;});
    }
  }
  // While joined: run cloud sessions with backoff; offline without identity.
  void online() {
    nextAttemptAt_=millis();
    while(WiFi.status()==WL_CONNECTED&&!commandWaiting()){
      takeIdentity();
      if(cloud_.configured()&&int32_t(millis()-nextAttemptAt_)>=0){session();continue;}
      const int8_t rssi=int8_t(WiFi.RSSI());
      edit([&](NetStatus& s){s.rssi=rssi;});
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    cloud_.stop();setCloud(CloudState::Off,status_.lastError);
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
        if(!commandWaiting()){
          setWifi(WifiState::Joining,NetError::None,0);
          Serial.println("net event=wifi_lost");
        }
        waitMs=kRejoinMs;
      }
      // Wait before retrying, but react at once to new settings.
      takeCommand(commandWaiting()?0:waitMs);
    }
  }
};
}
