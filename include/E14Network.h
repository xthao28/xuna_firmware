#pragma once
// E14 network probe (DC-1, ADR-0010). Built only with XUNA_E14=1.
// Wi-Fi + TLS (pinned LAN dev CA) + WebSocket echo in one FreeRTOS task on
// core 0, next to the Wi-Fi/BT/lwIP tasks. The life loop on core 1 only
// copies a counter snapshot; nothing here touches life state, NVS or BLE.
// esp_websocket_client is not used: in IDF 4.4 it starts its own unpinned
// task (priority 5) that may run on core 1 and hold the loop during a TLS
// handshake. esp_transport_ws is the same component's transport layer.
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <esp_tls_errors.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_ws.h>
#include <xuna/net/EchoProbe.h>
#if !__has_include("E14DevCa.h")
#error "XUNA_E14=1 needs include/E14DevCa.h: run backend/scripts/make_dev_ca.py --firmware-header (docs/roadmap/cloud/E14-runbook.md)"
#endif
#include "E14DevCa.h"
#if !defined(XUNA_E14_WIFI_SSID) || !defined(XUNA_E14_WIFI_PASS) || !defined(XUNA_E14_HOST)
#error "XUNA_E14=1 needs XUNA_E14_WIFI_SSID, XUNA_E14_WIFI_PASS and XUNA_E14_HOST (firmware/e14_local.ini)"
#endif
// Wi-Fi events must not run on the life loop core (platformio.ini xuna_e14).
static_assert(ARDUINO_EVENT_RUNNING_CORE==0,"xuna_e14 moves the Arduino event task to core 0");
#ifndef XUNA_E14_PORT
#define XUNA_E14_PORT 8765
#endif
namespace xuna {
struct E14NetStats {
  uint8_t state=0;  // 0 wifi_joining, 1 connecting, 2 ready, 3 backoff
  int8_t rssi=0;
  uint32_t wifiJoins=0,wifiDrops=0,connects=0,connectFails=0,disconnects=0;
  uint32_t lastConnectMs=0,maxConnectMs=0,lastReconnectMs=0,maxReconnectMs=0;
  uint32_t sent=0,ok=0,bad=0,lost=0,dropped=0,rttMaxMs=0,rttMeanMs=0;
  uint32_t stackFreeBytes=0;
  int lastErrno=0,lastTlsError=0,lastTlsFlags=0;
};
class E14Network {
  static constexpr uint32_t kStackBytes=8192;  // TLS handshake + cert verify; high water logged
  static constexpr UBaseType_t kPriority=1;     // below Wi-Fi, BT and lwIP on core 0
  static constexpr int kConnectTimeoutMs=10000,kIoTimeoutMs=2000,kPollMs=200;
  static constexpr uint32_t kBackoffMinMs=2000,kBackoffMaxMs=30000;
  esp_transport_handle_t ssl_=nullptr,ws_=nullptr;
  TaskHandle_t task_=nullptr;
  EchoProbe probe_{5000};
  char tx_[EchoProbe::kSize],rx_[EchoProbe::kSize];
  mutable portMUX_TYPE lock_=portMUX_INITIALIZER_UNLOCKED;
  E14NetStats stats_;
  template<class F> void edit(F f){portENTER_CRITICAL(&lock_);f(stats_);portEXIT_CRITICAL(&lock_);}
  void setState(uint8_t s){edit([&](E14NetStats& t){t.state=s;});}
  // Radio and RTOS calls stay outside the critical section.
  void publishProbe(){
    const int8_t rssi=int8_t(WiFi.RSSI());const uint32_t stack=uxTaskGetStackHighWaterMark(nullptr);
    edit([&](E14NetStats& t){
      t.sent=probe_.sent();t.ok=probe_.ok();t.bad=probe_.bad();t.lost=probe_.lost();t.dropped=probe_.dropped();
      t.rttMaxMs=probe_.rttMaxMs();t.rttMeanMs=probe_.rttMeanMs();
      t.stackFreeBytes=stack;t.rssi=rssi;
    });
  }
  uint32_t jittered(uint32_t ms){return ms-ms/4+esp_random()%(ms/2+1);}  // ±25 %
  static void run(void* self){static_cast<E14Network*>(self)->loop();}
  // One whole frame into rx_; -1 ends the session. Echo frames are ≤ 1 KB.
  int readFrame(){
    const int n=esp_transport_read(ws_,rx_,sizeof(rx_),kPollMs);
    if(n<=0)return n;
    const int total=esp_transport_ws_get_read_payload_len(ws_);
    if(total>int(sizeof(rx_)))return -1;
    int got=n;
    while(got<total){
      const int m=esp_transport_read(ws_,rx_+got,sizeof(rx_)-got,kIoTimeoutMs);
      if(m<=0)return -1;
      got+=m;
    }
    switch(esp_transport_ws_get_read_opcode(ws_)&0x0f){
      case WS_TRANSPORT_OPCODES_TEXT:probe_.received(rx_,size_t(got),millis());publishProbe();break;
      case WS_TRANSPORT_OPCODES_PING:
        if(esp_transport_ws_send_raw(ws_,ws_transport_opcodes_t(WS_TRANSPORT_OPCODES_PONG|WS_TRANSPORT_OPCODES_FIN),
                                     rx_,got,kIoTimeoutMs)<0)return -1;
        break;
      case WS_TRANSPORT_OPCODES_CLOSE:return -1;
      default:break;
    }
    return got;
  }
  const char* session(){
    for(;;){
      if(WiFi.status()!=WL_CONNECTED)return "wifi_lost";
      if(probe_.lostInRow()>=2)return "echo_timeout";
      const uint32_t now=millis();
      if(probe_.due(now)){
        probe_.begin(now,tx_);
        const int sent=esp_transport_ws_send_raw(ws_,ws_transport_opcodes_t(WS_TRANSPORT_OPCODES_TEXT|WS_TRANSPORT_OPCODES_FIN),
                                                 tx_,EchoProbe::kSize,kIoTimeoutMs);
        publishProbe();
        if(sent<0)return "send_failed";
      }
      if(readFrame()<0)return "read_closed";
    }
  }
  void loop(){
    WiFi.persistent(false);  // never write Wi-Fi credentials to NVS (the life snapshot lives there)
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(XUNA_E14_WIFI_SSID,XUNA_E14_WIFI_PASS);
    ssl_=esp_transport_ssl_init();
    esp_transport_ssl_set_cert_data(ssl_,kE14CaPem,int(std::strlen(kE14CaPem)));
    ws_=esp_transport_ws_init(ssl_);
    esp_transport_ws_config_t config={};config.ws_path="/e14";config.propagate_control_frames=true;
    esp_transport_ws_set_config(ws_,&config);
    Serial.printf("e14_net event=start host=%s port=%u ca=%s core=%d\n",XUNA_E14_HOST,unsigned(XUNA_E14_PORT),
                  XUNA_E14_CA_FINGERPRINT,int(xPortGetCoreID()));
    bool wifiUp=false,everReady=false;
    uint32_t lostAt=millis(),backoffMs=kBackoffMinMs;
    for(;;){
      const bool up=WiFi.status()==WL_CONNECTED;
      if(up!=wifiUp){
        wifiUp=up;
        const int8_t rssi=int8_t(WiFi.RSSI());
        edit([&](E14NetStats& t){if(up)++t.wifiJoins;else ++t.wifiDrops;t.rssi=rssi;});
        Serial.printf("e14_net event=%s rssi=%d\n",up?"wifi_up":"wifi_down",int(rssi));
      }
      if(!up){setState(0);vTaskDelay(pdMS_TO_TICKS(500));continue;}
      setState(1);
      const uint32_t started=millis();
      const int fd=esp_transport_connect(ws_,XUNA_E14_HOST,XUNA_E14_PORT,kConnectTimeoutMs);
      const uint32_t took=millis()-started;
      if(fd<0){
        const esp_tls_error_handle_t tls=esp_transport_get_error_handle(ws_);
        const int err=esp_transport_get_errno(ws_);
        const int tlsCode=tls?tls->esp_tls_error_code:0,tlsFlags=tls?tls->esp_tls_flags:0;
        edit([&](E14NetStats& t){++t.connectFails;t.lastErrno=err;t.lastTlsError=tlsCode;t.lastTlsFlags=tlsFlags;t.state=3;});
        Serial.printf("e14_net event=connect_failed ms=%u errno=%d tls=-0x%04x flags=0x%x backoff_ms=%u\n",
                      unsigned(took),err,unsigned(-tlsCode),unsigned(tlsFlags),unsigned(backoffMs));
        esp_transport_close(ws_);
        vTaskDelay(pdMS_TO_TICKS(jittered(backoffMs)));
        backoffMs=backoffMs*2>kBackoffMaxMs?kBackoffMaxMs:backoffMs*2;
        continue;
      }
      const uint32_t reconnectMs=millis()-lostAt;
      edit([&](E14NetStats& t){
        ++t.connects;t.lastConnectMs=took;if(took>t.maxConnectMs)t.maxConnectMs=took;t.state=2;
        if(everReady){t.lastReconnectMs=reconnectMs;if(reconnectMs>t.maxReconnectMs)t.maxReconnectMs=reconnectMs;}
      });
      Serial.printf("e14_net event=ready connect_ms=%u reconnect_ms=%u heap_int_free=%u\n",unsigned(took),
                    everReady?unsigned(reconnectMs):0u,unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
      everReady=true;backoffMs=kBackoffMinMs;
      const char* reason=session();
      esp_transport_close(ws_);
      probe_.disconnected();publishProbe();
      lostAt=millis();
      edit([&](E14NetStats& t){++t.disconnects;t.state=3;});
      Serial.printf("e14_net event=disconnected reason=%s\n",reason);
      vTaskDelay(pdMS_TO_TICKS(jittered(kBackoffMinMs)));
    }
  }
 public:
  bool begin(){return xTaskCreatePinnedToCore(&E14Network::run,"xuna_e14",kStackBytes,this,kPriority,&task_,0)==pdPASS;}
  E14NetStats stats() const {portENTER_CRITICAL(&lock_);const E14NetStats s=stats_;portEXIT_CRITICAL(&lock_);return s;}
};
}
