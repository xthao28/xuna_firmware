#include <unity.h>
#include <cstring>
#include <xuna/LifeEngine.h>
#include "../fixtures/v1_snapshots.h"
#include <xuna/identity/IdentityService.h>
#include <xuna/identity/IdentityCodec.h>
using namespace xuna;
IdentityEntropy entropy() { IdentityEntropy e;for(unsigned i=0;i<16;++i)e.petId[i]=i;e.seed=42;return e; }
void crc_and_codec() {
 const uint8_t text[]="123456789";TEST_ASSERT_EQUAL_HEX32(0xCBF43926,crc32(text,9));
 IdentityRecord record;record.hatchBootCounter=4;record.origin=IdentityOrigin::HatchedOnDevice;
 auto e=entropy();std::memcpy(record.petId,e.petId,16);record.petId[6]=0x46;record.petId[8]=0x88;
 record.seed=42;record.hatchMonoMs=(uint64_t(1)<<32)+99;record.hatchActiveSecond=12;
 uint8_t bytes[IdentityRecord::kEncodedSize];TEST_ASSERT_TRUE(record.encode(bytes));
 TEST_ASSERT_EQUAL_UINT32(64,sizeof(bytes));TEST_ASSERT_EQUAL_UINT8('X',bytes[0]);TEST_ASSERT_EQUAL_UINT8('I',bytes[1]);
 TEST_ASSERT_EQUAL_UINT8(42,bytes[20]);TEST_ASSERT_EQUAL_UINT8(4,bytes[24]);
 IdentityRecord decoded;TEST_ASSERT_EQUAL(IdentityLoad::Valid,decoded.decode(bytes,sizeof(bytes)));
 TEST_ASSERT_EQUAL_UINT64(record.hatchMonoMs,decoded.hatchMonoMs);TEST_ASSERT_EQUAL_UINT32(12,decoded.hatchActiveSecond);
 uint8_t reencoded[64];decoded.encode(reencoded);TEST_ASSERT_EQUAL_MEMORY(bytes,reencoded,64);
 for(size_t i=0;i<64;++i){uint8_t corrupt[64];std::memcpy(corrupt,bytes,64);corrupt[i]^=1;TEST_ASSERT_EQUAL(IdentityLoad::Corrupt,decoded.decode(corrupt,64));}
 TEST_ASSERT_EQUAL(IdentityLoad::Corrupt,decoded.decode(nullptr,64));
 bytes[20]^=1;TEST_ASSERT_EQUAL(IdentityLoad::Corrupt,decoded.decode(bytes,64));TEST_ASSERT_EQUAL_UINT32(42,decoded.seed);bytes[20]^=1;
 bytes[2]=2;IdentityRecord::put32(bytes+60,crc32(bytes,60));
 TEST_ASSERT_EQUAL(IdentityLoad::TooNew,decoded.decode(bytes,64));
 TEST_ASSERT_EQUAL(IdentityLoad::Corrupt,decoded.decode(bytes,63));
 bytes[2]=1;bytes[51]=99;IdentityRecord::put32(bytes+60,crc32(bytes,60));
 TEST_ASSERT_EQUAL(IdentityLoad::Corrupt,decoded.decode(bytes,64));
 record.bornAtUtcMs=1800000000000LL;record.bornAtQuality=ClockQuality::Synced;
 TEST_ASSERT_TRUE(record.encode(bytes));TEST_ASSERT_EQUAL(IdentityLoad::Valid,decoded.decode(bytes,64));
 TEST_ASSERT_EQUAL_INT64(record.bornAtUtcMs,decoded.bornAtUtcMs);
 record.bornAtQuality=ClockQuality::Unknown;TEST_ASSERT_FALSE(record.encode(bytes));
}
void hatch_and_migration() {
 IdentityService service;service.onBoot(1,IdentityLoad::Missing,nullptr,false);
 TEST_ASSERT_EQUAL(IdentityState::Egg,service.state());
 ClockModel clock;clock.onBoot();IdentityRecord candidate;
 TEST_ASSERT_TRUE(service.prepareHatch(entropy(),IdentityOrigin::HatchedOnDevice,5,100,clock,candidate));
 TEST_ASSERT_EQUAL_UINT8(0x40,candidate.petId[6]&0xF0);TEST_ASSERT_EQUAL_UINT8(0x80,candidate.petId[8]&0xC0);
 TEST_ASSERT_EQUAL_UINT32(1,candidate.hatchBootCounter);TEST_ASSERT_EQUAL_INT64(0,candidate.bornAtUtcMs);
 TEST_ASSERT_EQUAL(IdentityState::Egg,service.state()); // prepare is not a successful storage commit
 TEST_ASSERT_TRUE(service.acceptPersisted(candidate));TEST_ASSERT_EQUAL(IdentityState::Ready,service.state());
 TEST_ASSERT_FALSE(service.prepareHatch(entropy(),IdentityOrigin::HatchedOnDevice,5,100,clock,candidate));
 LifeEngine life;TEST_ASSERT_TRUE(life.decode(xuna_fixture::scripted,96));uint8_t before[96],after[96];life.encode(before);
 IdentityService migrated;migrated.onBoot(2,IdentityLoad::Missing,nullptr,true);
 TEST_ASSERT_EQUAL(IdentityState::NeedsMigration,migrated.state());
 TEST_ASSERT_TRUE(migrated.prepareMigration(entropy(),123,900,candidate));
 TEST_ASSERT_EQUAL(IdentityOrigin::MigratedFromV1,candidate.origin);TEST_ASSERT_EQUAL_INT64(0,candidate.bornAtUtcMs);
 TEST_ASSERT_TRUE(migrated.acceptPersisted(candidate));life.encode(after);TEST_ASSERT_EQUAL_MEMORY(before,after,96);
 IdentityRecord replacement=candidate;replacement.seed++;TEST_ASSERT_FALSE(migrated.acceptPersisted(replacement));
 service.onBoot(3,IdentityLoad::Valid,&candidate,true);TEST_ASSERT_EQUAL(IdentityState::Ready,service.state());
 TEST_ASSERT_EQUAL_UINT32(2,service.record().hatchBootCounter); // loaded identity unchanged
 service.onBoot(4,IdentityLoad::Corrupt,nullptr,true);TEST_ASSERT_EQUAL(IdentityState::Safe,service.state());
 TEST_ASSERT_FALSE(service.prepareMigration(entropy(),1,1,candidate));
 service.onBoot(4,IdentityLoad::TooNew,nullptr,false);TEST_ASSERT_EQUAL(IdentityState::Safe,service.state());
 service.onBoot(4,IdentityLoad::Valid,nullptr,false);TEST_ASSERT_EQUAL(IdentityState::Safe,service.state());
}
void birth_time() {
 IdentityService service;service.onBoot(9,IdentityLoad::Missing,nullptr,false);
 ClockModel clock;clock.onBoot(0);const int64_t utc=1800000000000LL;
 clock.onSync(utc,420,TimeSource::Phone,1000);IdentityRecord record;
 TEST_ASSERT_TRUE(service.prepareHatch(entropy(),IdentityOrigin::HatchedViaApp,10,2000,clock,record));
 TEST_ASSERT_EQUAL_INT64(utc+1000,record.bornAtUtcMs);TEST_ASSERT_EQUAL(ClockQuality::Synced,record.bornAtQuality);
 service.acceptPersisted(record);IdentityRecord updated;TEST_ASSERT_FALSE(service.prepareBornAt(clock,3000,updated));
 ClockModel unknown;unknown.onBoot();service.onBoot(10,IdentityLoad::Missing,nullptr,false);
 service.prepareHatch(entropy(),IdentityOrigin::HatchedOnDevice,0,5000,unknown,record);service.acceptPersisted(record);
 unknown.onSync(utc,0,TimeSource::Phone,8000);
 TEST_ASSERT_TRUE(service.prepareBornAt(unknown,8000,updated));TEST_ASSERT_EQUAL_INT64(utc-3000,updated.bornAtUtcMs);
 TEST_ASSERT_EQUAL_INT64(0,service.record().bornAtUtcMs); // failed persistence must leave record unknown
 service.onBoot(11,IdentityLoad::Valid,&record,false);
 TEST_ASSERT_FALSE(service.prepareBornAt(unknown,8000,updated)); // later boot must never backfill
 service.onBoot(10,IdentityLoad::Valid,&record,false);service.acceptPersisted(updated);
 TEST_ASSERT_FALSE(service.prepareBornAt(unknown,9000,updated)); // fills at most once
 service.onBoot(12,IdentityLoad::Missing,nullptr,true);service.prepareMigration(entropy(),99,0,record);service.acceptPersisted(record);
 TEST_ASSERT_FALSE(service.prepareBornAt(unknown,8000,updated)); // historical MVP birth never invented
}
void approximate_and_failure() {
 IdentityService service;ClockModel clock;clock.onBoot(0);
 clock.onSync(1800000000000LL,0,TimeSource::Phone,0);
 const uint64_t mono=ClockModel::kValidityMs+1;
 service.onBoot(7,IdentityLoad::Missing,nullptr,false);IdentityRecord record;
 TEST_ASSERT_TRUE(service.prepareHatch(entropy(),IdentityOrigin::HatchedOnDevice,0,mono,clock,record));
 TEST_ASSERT_EQUAL_INT64(0,record.bornAtUtcMs);TEST_ASSERT_TRUE(service.acceptPersisted(record));
 IdentityRecord update;TEST_ASSERT_FALSE(service.prepareBornAt(clock,mono,update));
 clock.onSync(1800000000000LL+int64_t(mono)+1000,0,TimeSource::Phone,mono+1000);
 TEST_ASSERT_TRUE(service.prepareBornAt(clock,mono+1000,update));TEST_ASSERT_EQUAL_INT64(1800000000000LL+int64_t(mono),update.bornAtUtcMs);
 TEST_ASSERT_FALSE(service.prepareBornAt(clock,mono-1,update));
 service.onBoot(0,IdentityLoad::Missing,nullptr,false);TEST_ASSERT_EQUAL(IdentityState::Safe,service.state());
 TEST_ASSERT_FALSE(service.prepareHatch(entropy(),IdentityOrigin::HatchedOnDevice,0,0,clock,record));
}
void identity_characteristic() {
 IdentityRecord record;record.hatchBootCounter=3;record.origin=IdentityOrigin::MigratedFromV1;
 for(unsigned i=0;i<16;++i)record.petId[i]=uint8_t(0xa0+i);
 record.petId[6]=0x46;record.petId[8]=0x88;
 uint8_t bytes[IdentityCodec::kSize];std::memset(bytes,0xee,sizeof(bytes));
 TEST_ASSERT_EQUAL_UINT32(24,sizeof(bytes));
 TEST_ASSERT_TRUE(IdentityCodec::encode(&record,0x01020304,bytes));
 const uint8_t expected[24]={1,24,0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0x46,0xa7,0x88,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
  3,0,0x04,0x03,0x02,0x01};
 TEST_ASSERT_EQUAL_MEMORY(expected,bytes,24);
 record.origin=IdentityOrigin::HatchedViaApp;TEST_ASSERT_TRUE(IdentityCodec::encode(&record,0xffffffffU,bytes));
 TEST_ASSERT_EQUAL_UINT8(2,bytes[18]);TEST_ASSERT_EQUAL_HEX32(0xffffffffU,IdentityRecord::get32(bytes+20));
 const uint8_t empty[24]={1,24};
 std::memset(bytes,0xee,sizeof(bytes));TEST_ASSERT_FALSE(IdentityCodec::encode(nullptr,7,bytes));
 TEST_ASSERT_EQUAL_MEMORY(empty,bytes,24);
 record.hatchBootCounter=0;std::memset(bytes,0xee,sizeof(bytes));
 TEST_ASSERT_FALSE(IdentityCodec::encode(&record,7,bytes));TEST_ASSERT_EQUAL_MEMORY(empty,bytes,24);
 IdentityService service;service.onBoot(1,IdentityLoad::Missing,nullptr,false);
 TEST_ASSERT_EQUAL(IdentityState::Egg,service.state());
 TEST_ASSERT_FALSE(IdentityCodec::encode(service.state()==IdentityState::Ready?&service.record():nullptr,9,bytes));
}
void setUp() {} void tearDown() {}
int main(){UNITY_BEGIN();RUN_TEST(crc_and_codec);RUN_TEST(hatch_and_migration);RUN_TEST(birth_time);RUN_TEST(approximate_and_failure);RUN_TEST(identity_characteristic);return UNITY_END();}
