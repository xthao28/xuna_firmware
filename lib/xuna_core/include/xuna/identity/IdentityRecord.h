#pragma once
#include <cstring>
#include <xuna/base/Crc32.h>
#include <xuna/time/ClockModel.h>
namespace xuna {
enum class IdentityOrigin : uint8_t { HatchedOnDevice=1,HatchedViaApp=2,MigratedFromV1=3 };
enum class IdentityLoad : uint8_t { Missing,Valid,Corrupt,TooNew };
struct IdentityRecord {
 static constexpr size_t kEncodedSize=64;
 uint8_t petId[16]={};uint32_t seed=0,hatchBootCounter=0,hatchActiveSecond=0;
 uint64_t hatchMonoMs=0;int64_t bornAtUtcMs=0;
 ClockQuality bornAtQuality=ClockQuality::Unknown;
 uint16_t hatchContentVersion=1;
 IdentityOrigin origin=IdentityOrigin::HatchedOnDevice;
 static void put32(uint8_t* b,uint32_t value){for(unsigned i=0;i<4;++i)b[i]=value>>(8*i);}
 static uint32_t get32(const uint8_t* b){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(b[i])<<(8*i);return value;}
 static void put64(uint8_t* b,uint64_t value){for(unsigned i=0;i<8;++i)b[i]=value>>(8*i);}
 static uint64_t get64(const uint8_t* b){uint64_t value=0;for(unsigned i=0;i<8;++i)value|=uint64_t(b[i])<<(8*i);return value;}
 bool valid() const {
  return hatchBootCounter!=0&&hatchContentVersion!=0&&(petId[6]&0xF0)==0x40&&(petId[8]&0xC0)==0x80&&
   (origin==IdentityOrigin::HatchedOnDevice||origin==IdentityOrigin::HatchedViaApp||origin==IdentityOrigin::MigratedFromV1)&&
   ((bornAtUtcMs==0&&bornAtQuality==ClockQuality::Unknown)||
    (bornAtUtcMs>=ClockModel::kEpochUtcMs&&bornAtQuality==ClockQuality::Synced&&origin!=IdentityOrigin::MigratedFromV1));
 }
 bool encode(uint8_t* b) const {
  if(!b||!valid())return false;
  std::memset(b,0,kEncodedSize);b[0]='X';b[1]='I';b[2]=1;
  std::memcpy(b+4,petId,16);put32(b+20,seed);put32(b+24,hatchBootCounter);put32(b+28,hatchActiveSecond);
  put64(b+32,hatchMonoMs);put64(b+40,static_cast<uint64_t>(bornAtUtcMs));b[48]=static_cast<uint8_t>(bornAtQuality);
  b[49]=hatchContentVersion&255;b[50]=hatchContentVersion>>8;b[51]=static_cast<uint8_t>(origin);
  put32(b+60,crc32(b,60));return true;
 }
 IdentityLoad decode(const uint8_t* b,size_t n) {
  if(!b||n!=kEncodedSize||b[0]!='X'||b[1]!='I'||get32(b+60)!=crc32(b,60))return IdentityLoad::Corrupt;
  if(b[2]>1)return IdentityLoad::TooNew;
  if(b[2]!=1||b[3]!=0)return IdentityLoad::Corrupt;
  for(size_t i=52;i<60;++i)if(b[i])return IdentityLoad::Corrupt;
  IdentityRecord candidate;std::memcpy(candidate.petId,b+4,16);
  candidate.seed=get32(b+20);candidate.hatchBootCounter=get32(b+24);candidate.hatchActiveSecond=get32(b+28);
  candidate.hatchMonoMs=get64(b+32);const uint64_t born=get64(b+40);
  if(born>uint64_t(INT64_MAX))return IdentityLoad::Corrupt;
  candidate.bornAtUtcMs=static_cast<int64_t>(born);candidate.bornAtQuality=static_cast<ClockQuality>(b[48]);
  candidate.hatchContentVersion=uint16_t(b[49])|(uint16_t(b[50])<<8);candidate.origin=static_cast<IdentityOrigin>(b[51]);
  if(!candidate.valid())return IdentityLoad::Corrupt;
  *this=candidate;return IdentityLoad::Valid;
 }
};
}
