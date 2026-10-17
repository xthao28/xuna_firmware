#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <xuna/identity/IdentityRecord.h>
namespace xuna {
// Identity V1 (characteristic 7a6e000c, capability bit 7), v1-additive, 24 bytes:
// 0 ver=1, 1 size=24, 2..17 petId, 18 origin (IdentityOrigin), 19 reserved=0,
// 20..23 snapshotGeneration u32 LE. Without a Ready identity (null or invalid
// record) only ver/size are set and the rest is zero, like ExplanationCodec.
class IdentityCodec {
 public:
  static constexpr size_t kSize=24;
  static bool encode(const IdentityRecord* record,uint32_t generation,uint8_t out[kSize]){
   std::memset(out,0,kSize);out[0]=1;out[1]=kSize;
   if(!record||!record->valid())return false;
   std::memcpy(out+2,record->petId,16);out[18]=static_cast<uint8_t>(record->origin);
   IdentityRecord::put32(out+20,generation);return true;
  }
};
}  // namespace xuna
