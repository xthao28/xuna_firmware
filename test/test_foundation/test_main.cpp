#include <unity.h>
#include <initializer_list>
#include <xuna/base/Fixed.h>
#include <xuna/base/Random.h>
#include <xuna/time/ClockModel.h>
using namespace xuna;
void fixed_math() {
 TEST_ASSERT_EQUAL_UINT16(0,Q0_16::fromInteger(0).raw());
 TEST_ASSERT_EQUAL_UINT16(65535,Q0_16::fromInteger(1).raw());
 TEST_ASSERT_EQUAL_UINT32(65536,Q16_16::fromInteger(1).raw());
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,Q16_16::fromInteger(UINT32_MAX).raw());
 TEST_ASSERT_EQUAL_UINT16(32768,Q0_16::fromRatio(1,2).raw());
 TEST_ASSERT_EQUAL_UINT32(98304,Q16_16::fromRatio(3,2).raw());
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,Q16_16::fromRatio(UINT32_MAX,1).raw());
 TEST_ASSERT_EQUAL_UINT32(0,Q16_16::fromRatio(0,0).raw());
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,Q16_16::fromRatio(1,0).raw());
 TEST_ASSERT_EQUAL_UINT32(1,Q16_16::fromRatio(1,131072).raw()); // half-up
 TEST_ASSERT_EQUAL_UINT16(16384,Q0_16::fromRatio(1,2).multiply(Q0_16::fromRatio(1,2)).raw());
 TEST_ASSERT_EQUAL_UINT32(196608,Q16_16::fromRatio(3,2).multiply(Q16_16::fromInteger(2)).raw());
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,Q16_16::fromRaw(UINT32_MAX).multiply(Q16_16::fromRaw(UINT32_MAX)).raw());
 TEST_ASSERT_EQUAL_UINT32(1,Q16_16::fromRaw(1).multiply(Q16_16::fromRaw(32768)).raw());
 TEST_ASSERT_EQUAL_UINT32(32768,Q16_16::fromInteger(1).divide(Q16_16::fromInteger(2)).raw());
 TEST_ASSERT_EQUAL_UINT32(1,Q16_16::fromRaw(1).divide(Q16_16::fromInteger(2)).raw());
 TEST_ASSERT_EQUAL_UINT16(65535,Q0_16::fromRatio(1,2).divide(Q0_16::fromRatio(1,2)).raw());
 TEST_ASSERT_EQUAL_UINT32(0,Q16_16::fromRaw(0).divide(Q16_16::fromRaw(0)).raw());
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,Q16_16::fromInteger(1).divide(Q16_16::fromRaw(0)).raw());
 TEST_ASSERT_EQUAL_UINT16(65535,Q0_16::fromRaw(65000).add(Q0_16::fromRaw(1000)).raw());
 TEST_ASSERT_EQUAL_UINT32(0,Q16_16::fromRaw(5).subtract(Q16_16::fromRaw(6)).raw());
 TEST_ASSERT_EQUAL_UINT32(10,Q16_16::fromRaw(5).clamp(Q16_16::fromRaw(10),Q16_16::fromRaw(20)).raw());
 TEST_ASSERT_EQUAL_UINT32(20,Q16_16::fromRaw(25).clamp(Q16_16::fromRaw(10),Q16_16::fromRaw(20)).raw());
}
void rng_reference() {
 // Official pcg32-demo Round 1 (seed=42, stream=54), copied from:
 // https://www.pcg-random.org/using-pcg-c-basic.html
 const uint32_t expected[]={0xa15c02b7,0x7b47f409,0xba1d3330,0x83d2f293,0xbfa4784b,0xcbed606e};
 Pcg32 rng(42,54);for(auto v:expected)TEST_ASSERT_EQUAL_UINT32(v,rng.next());
 Pcg32 a(123,4),b(123,4),other(123,5);bool differs=false;
 for(int i=0;i<100;++i){uint32_t v=a.next();TEST_ASSERT_EQUAL_UINT32(v,b.next());differs|=v!=other.next();}TEST_ASSERT_TRUE(differs);
 // Force rejection below threshold for bound 6 (threshold=4).
 class Scripted : public IRandom { public: uint32_t calls=0; uint32_t next() override {const uint32_t values[]={0,3,4};return values[calls++];} };
 Scripted scripted;TEST_ASSERT_EQUAL_UINT32(4,scripted.below(6));TEST_ASSERT_EQUAL_UINT32(3,scripted.calls);
 Scripted zero;TEST_ASSERT_EQUAL_UINT32(0,zero.below(0));TEST_ASSERT_EQUAL_UINT32(0,zero.calls);
 uint32_t counts[6]={};Pcg32 dice(42,54);
 for(int i=0;i<60000;++i){uint32_t v=dice.below(6);TEST_ASSERT_LESS_THAN_UINT32(6,v);++counts[v];}
 for(auto n:counts)TEST_ASSERT_TRUE(n>9500&&n<10500);
 TEST_ASSERT_EQUAL_UINT32(0,dice.below(1));TEST_ASSERT_EQUAL_UINT32(0,dice.below(0));
 for(int i=0;i<1000;++i)TEST_ASSERT_LESS_THAN_UINT32(UINT32_MAX,dice.below(UINT32_MAX));
}
void clock_lifecycle() {
 ClockModel clock;clock.onBoot(1000);TEST_ASSERT_EQUAL(ClockQuality::Unknown,clock.quality(1000));
 TEST_ASSERT_EQUAL_UINT32(0,clock.utcMinute(1000));TEST_ASSERT_EQUAL(DayPart::Unknown,clock.dayPart(1000));
 const int64_t utc=1800000000000LL;const uint64_t mono=(uint64_t(1)<<32)+123;
 auto result=clock.onSync(utc,420,TimeSource::Phone,mono);TEST_ASSERT_EQUAL(SyncStatus::Accepted,result.status);TEST_ASSERT_FALSE(result.gap);
 TEST_ASSERT_EQUAL_UINT32(3702720,clock.lastTrustedUtcMinute());
 TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,clock.onSync(utc,0,TimeSource::Phone,mono-1).status);
 TEST_ASSERT_EQUAL_UINT32(3702721,clock.utcMinute(mono+60000));
 TEST_ASSERT_EQUAL(ClockQuality::Synced,clock.quality(mono+ClockModel::kValidityMs));
 TEST_ASSERT_EQUAL(ClockQuality::Approximate,clock.quality(mono+ClockModel::kValidityMs+1));
 TEST_ASSERT_NOT_EQUAL(DayPart::Unknown,clock.dayPart(mono+ClockModel::kValidityMs+1));
 TEST_ASSERT_EQUAL(ClockQuality::Unknown,clock.quality(mono-1));
 TEST_ASSERT_EQUAL(SyncStatus::RejectedRollback,clock.onSync(utc-300001,0,TimeSource::Phone,mono).status);
 TEST_ASSERT_EQUAL_INT64(utc,clock.utcMs(mono)); // rejected sync preserved anchor
 TEST_ASSERT_EQUAL(SyncStatus::RejectedRollback,clock.onSync(utc,0,TimeSource::Phone,mono+600000).status);
 TEST_ASSERT_EQUAL(SyncStatus::Accepted,clock.onSync(utc-300000,0,TimeSource::Phone,mono).status);
 TEST_ASSERT_EQUAL_INT64(utc,clock.utcMs(mono)); // tolerated rollback never moves wall time backwards
 result=clock.onSync(utc+600000,0,TimeSource::Ntp,mono);TEST_ASSERT_TRUE(result.gap);
 TEST_ASSERT_EQUAL(TimeSource::Ntp,clock.source());
 const uint32_t saved=clock.lastTrustedUtcMinute();clock.onBoot(0,saved);
 TEST_ASSERT_EQUAL_UINT32(0,clock.utcMinute(100000)); // never invent elapsed offline time
 TEST_ASSERT_EQUAL_UINT32(saved,clock.lastTrustedUtcMinute());
 TEST_ASSERT_EQUAL(SyncStatus::RejectedRollback,clock.onSync(utc,0,TimeSource::Phone,100000).status);
 TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,clock.onSync(-1,0,TimeSource::Phone,100000).status);
 TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,clock.onSync(utc,841,TimeSource::Phone,100000).status);
 TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,clock.onSync(utc,0,TimeSource::Unknown,100000).status);
 TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,clock.onSync(INT64_MAX,0,TimeSource::Phone,100000).status);
}
void clock_dayparts() {
 struct Case {uint16_t minute;DayPart part;};
 const Case cases[]={{0,DayPart::Night},{299,DayPart::Night},{300,DayPart::Morning},{659,DayPart::Morning},{660,DayPart::Afternoon},{1019,DayPart::Afternoon},{1020,DayPart::Evening},{1319,DayPart::Evening},{1320,DayPart::Night},{1439,DayPart::Night}};
 for(const auto& c:cases){ClockModel clock;clock.onBoot(0);clock.onSync(1577836800000LL+86400000LL+int64_t(c.minute)*60000,0,TimeSource::Phone,0);TEST_ASSERT_EQUAL(c.part,clock.dayPart(0));}
 ClockModel clock;clock.onBoot(0);clock.onSync(1577923200000LL,420,TimeSource::Phone,0);TEST_ASSERT_EQUAL(DayPart::Morning,clock.dayPart(0));
 clock.onSync(1577923200000LL,-60,TimeSource::Phone,0);TEST_ASSERT_EQUAL(DayPart::Night,clock.dayPart(0));
 clock.onSync(1577836860000LL,-60,TimeSource::Phone,0); // rollback refused, timezone stays unchanged
 TEST_ASSERT_EQUAL_INT16(-60,clock.tzOffsetMin());
 ClockModel early;early.onBoot(0);early.onSync(1577836860000LL,-60,TimeSource::Phone,0);TEST_ASSERT_EQUAL(DayPart::Night,early.dayPart(0));
 ClockModel far;far.onBoot(0);far.onSync(1577836860000LL,0,TimeSource::Phone,0);
 TEST_ASSERT_EQUAL_UINT32(0,far.utcMinute(UINT64_MAX)); // overflow cannot create plausible time
}
void clock_epoch_bounds() {
 const int64_t epoch=1577836800000LL; // 2020-01-01T00:00:00Z, Unix milliseconds
 ClockModel clock;clock.onBoot(0);
 for(const int64_t invalid: {epoch-1,epoch,epoch+59999}) {
  TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,clock.onSync(invalid,0,TimeSource::Phone,0).status);
  TEST_ASSERT_EQUAL(ClockQuality::Unknown,clock.quality(0));
 }
 TEST_ASSERT_EQUAL(SyncStatus::Accepted,clock.onSync(epoch+60000,0,TimeSource::Phone,0).status);
 TEST_ASSERT_EQUAL_INT64(epoch+60000,clock.utcMs(0));
 TEST_ASSERT_EQUAL_UINT32(1,clock.utcMinute(0));TEST_ASSERT_EQUAL_UINT32(1,clock.lastTrustedUtcMinute());
 TEST_ASSERT_EQUAL_UINT32(1,clock.utcMinute(59999));TEST_ASSERT_EQUAL_UINT32(2,clock.utcMinute(60000));
 clock.onBoot(0,10);TEST_ASSERT_EQUAL_UINT32(0,clock.utcMinute(500000));
 TEST_ASSERT_EQUAL(SyncStatus::RejectedRollback,clock.onSync(epoch+4*60000,0,TimeSource::Phone,0).status);
 TEST_ASSERT_EQUAL(SyncStatus::Accepted,clock.onSync(epoch+5*60000,0,TimeSource::Phone,0).status);
 TEST_ASSERT_EQUAL_UINT32(10,clock.utcMinute(0)); // watermark decoded in same epoch
 ClockModel upper;upper.onBoot(0);
 const int64_t maximum=epoch+int64_t(UINT32_MAX)*60000+59999;
 TEST_ASSERT_EQUAL(SyncStatus::Accepted,upper.onSync(maximum,0,TimeSource::Phone,0).status);
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,upper.utcMinute(0));
 TEST_ASSERT_EQUAL(ClockQuality::Unknown,upper.quality(1));
 TEST_ASSERT_EQUAL_UINT32(0,upper.utcMinute(1));
 TEST_ASSERT_EQUAL(SyncStatus::RejectedInvalid,upper.onSync(maximum+1,0,TimeSource::Phone,1).status);
}
void setUp() {} void tearDown() {}
int main(){UNITY_BEGIN();RUN_TEST(fixed_math);RUN_TEST(rng_reference);RUN_TEST(clock_lifecycle);RUN_TEST(clock_dayparts);RUN_TEST(clock_epoch_bounds);return UNITY_END();}
