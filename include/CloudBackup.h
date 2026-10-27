#pragma once
// Automatic projection backup (DC-5, XUNA_NET builds). Runs on the life loop:
// it only reads the same BLE projections the app reads, writes the
// `xuna.export.v1` frame into the network task's PSRAM slot, and never
// changes life state. Sent once per ready session and after a checkpoint
// raised the generation, at most every 15 min.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <xuna/identity/IdentityRecord.h>
#include <xuna/net/ExportWriter.h>
#include <xuna/protocol/V1LifeCoreAdapter.h>
#include "BoardConfig.h"
#include "NetManager.h"
namespace xuna {
class CloudBackup {
  static constexpr uint32_t kMinIntervalMs=15UL*60UL*1000UL;
  char* payload_=nullptr;   // PSRAM scratch
  char* envelope_=nullptr;
  uint32_t seenSessions_=0,sentAt_=0,sentGeneration_=0;
  bool sentOnce_=false;
 public:
  bool begin() {
    payload_=static_cast<char*>(heap_caps_malloc(ExportWriter::kMaxPayload,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    envelope_=static_cast<char*>(heap_caps_malloc(ExportWriter::kMaxEnvelope,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    return payload_&&envelope_;
  }
  void service(uint32_t now,const V1LifeCoreAdapter& adapter,const IdentityRecord* identity,NetManager& net) {
    if(!payload_||!identity||net.status().cloud!=CloudState::Ready||!net.projectionSlotFree())return;
    const uint32_t sessions=net.readySessions(),generation=adapter.generation();
    const bool newSession=sessions!=seenSessions_;
    const bool newer=generation!=sentGeneration_&&(!sentOnce_||uint32_t(now-sentAt_)>=kMinIntervalMs);
    if(!newSession&&!newer)return;
    seenSessions_=sessions;sentAt_=now;sentGeneration_=generation;sentOnce_=true;
    uint8_t life[20],pages[2][20],explanation[20];
    adapter.encodeLife(life);adapter.encodeMemory(0,pages[0]);adapter.encodeMemory(1,pages[1]);
    adapter.encodeExplanation(explanation);
    char version[48];std::snprintf(version,sizeof(version),"firmware/%s",kFirmwareVersion);
    ExportSource source;
    source.life=life;source.memoryPages[0]=pages[0];source.memoryPages[1]=pages[1];source.explanation=explanation;
    source.petId=identity->petId;source.origin=static_cast<uint8_t>(identity->origin);source.generation=generation;
    // The board's own UTC when the phone synced it; otherwise "unknown" (never a guess).
    source.exportedAtUtcMs=adapter.clock().quality(adapter.monoNow())==ClockQuality::Unknown
                               ?0:adapter.clock().utcMs(adapter.monoNow());
    source.version=version;
    const uint32_t started=micros();
    const size_t payload=ExportWriter::payload(source,payload_,ExportWriter::kMaxPayload);
    char hash[65]="";
    const size_t envelope=payload?ExportWriter::envelope(payload_,payload,envelope_,ExportWriter::kMaxEnvelope,hash):0;
    const size_t frame=envelope?ExportWriter::projectionFrame(generation,envelope_,envelope,net.projectionBuffer(),
                                                              ExportWriter::kMaxFrame):0;
    if(frame)net.offerProjection(frame,generation);
    Serial.printf("backup_built generation=%u bytes=%u ok=%u build_us=%u\n",unsigned(generation),unsigned(frame),
                  frame?1u:0u,unsigned(micros()-started));
  }
};
}
