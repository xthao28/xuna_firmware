#pragma once
// E14 measurements (DC-1). Built only when XUNA_E14 is defined: 0 = baseline
// (normal firmware + these logs), 1 = baseline + the network probe. Same
// lines in both runs so docs/roadmap/cloud tools can compare them.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <xuna/base/LatencyHistogram.h>
#if XUNA_E14
#include "E14Network.h"
#endif
namespace xuna {
class E14Diagnostics {
  static constexpr uint32_t kReportMs=30000;
  LatencyHistogram<201,1000> gaps_;     // loop gap, 1 ms buckets to 200 ms
  LatencyHistogram<101,1000> renders_;  // display.update() passes that drew
  uint32_t reportAt_=0;
#if XUNA_E14
  E14Network net_;
  bool netStarted_=false;
#endif
 public:
  // At the end of setup(): reset reason tells resets and brownouts apart.
  void begin(uint32_t now) {
    reportAt_=now;
    Serial.printf("e14_boot mode=%d reset_reason=%d heap_int_free=%u heap_int_largest=%u\n",int(XUNA_E14),
                  int(esp_reset_reason()),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
#if XUNA_E14
    netStarted_=net_.begin();
    if(!netStarted_)Serial.println("ERROR: e14 network task not started");
#endif
  }
  void loopGap(uint32_t us){gaps_.add(us);}
  void render(uint32_t us){renders_.add(us);}
  // Cumulative since boot; printed every 30 s.
  void report(uint32_t now,uint32_t bleCommands,uint32_t bleNonOk,uint32_t bleDropped,uint32_t bleMaxUs) {
    if(uint32_t(now-reportAt_)<kReportMs)return;
    reportAt_=now;
    Serial.printf("e14 t_s=%u heap_int_free=%u heap_int_min=%u heap_int_largest=%u psram_free=%u"
                  " gap_n=%u gap_p50_us=%u gap_p99_us=%u gap_max_us=%u render_n=%u render_mean_us=%u"
                  " render_max_us=%u ble_cmds=%u ble_nonok=%u ble_dropped=%u ble_max_us=%u\n",
                  unsigned(now/1000),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
                  unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
                  unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(gaps_.count()),
                  unsigned(gaps_.percentileUs(500)),unsigned(gaps_.percentileUs(990)),unsigned(gaps_.maxUs()),
                  unsigned(renders_.count()),unsigned(renders_.meanUs()),unsigned(renders_.maxUs()),
                  unsigned(bleCommands),unsigned(bleNonOk),unsigned(bleDropped),unsigned(bleMaxUs));
#if XUNA_E14
    if(!netStarted_)return;
    const auto s=net_.stats();
    Serial.printf("e14_net t_s=%u state=%u rssi=%d wifi_joins=%u wifi_drops=%u connects=%u connect_fails=%u"
                  " disconnects=%u connect_ms_last=%u connect_ms_max=%u reconnect_ms_last=%u reconnect_ms_max=%u"
                  " sent=%u ok=%u bad=%u lost=%u dropped=%u rtt_ms_max=%u rtt_ms_mean=%u stack_free=%u"
                  " errno=%d tls=-0x%04x flags=0x%x\n",
                  unsigned(now/1000),unsigned(s.state),int(s.rssi),unsigned(s.wifiJoins),unsigned(s.wifiDrops),
                  unsigned(s.connects),unsigned(s.connectFails),unsigned(s.disconnects),unsigned(s.lastConnectMs),
                  unsigned(s.maxConnectMs),unsigned(s.lastReconnectMs),unsigned(s.maxReconnectMs),unsigned(s.sent),
                  unsigned(s.ok),unsigned(s.bad),unsigned(s.lost),unsigned(s.dropped),unsigned(s.rttMaxMs),
                  unsigned(s.rttMeanMs),unsigned(s.stackFreeBytes),s.lastErrno,unsigned(-s.lastTlsError),
                  unsigned(s.lastTlsFlags));
#endif
  }
};
}
