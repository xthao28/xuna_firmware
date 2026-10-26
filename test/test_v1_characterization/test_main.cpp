#include <unity.h>
#include <cstring>
#include <initializer_list>
#include <xuna/LifeEngine.h>
#include <xuna/BleProtocol.h>
#include <xuna/LifecycleCodec.h>
#include <xuna/LocalBehavior.h>
#include "../fixtures/v1_snapshots.h"
using namespace xuna;
void snapshots() {
 const uint8_t* golden[]={xuna_fixture::fresh,xuna_fixture::scripted,xuna_fixture::saturated};
 for(int i=0;i<3;++i){
  LifeEngine life;TEST_ASSERT_TRUE(life.decode(golden[i],96));
  TEST_ASSERT_TRUE(life.generation==0&&life.activeSeconds==0);
  TEST_ASSERT_TRUE(life.eventCount==(i==1?4U:0U));TEST_ASSERT_TRUE(life.episodeCount==(i==1?4:0));
  TEST_ASSERT_TRUE(life.episodeNext==(i==1?4:0));
  for(int c=0;c<2;++c)for(int a=0;a<2;++a){
   TEST_ASSERT_TRUE(life.positive[c][a]==(i==2?1000:(i==1&&c==1&&a==1?3:0)));
   TEST_ASSERT_TRUE(life.negative[c][a]==(i==2?1000:(i==1&&c==0&&a==0?1:0)));
  }
  for(int e=0;e<8;++e){
   const auto& ep=life.episodes[e];
   TEST_ASSERT_TRUE(ep.context==(i==1&&e>0&&e<4?1:0));
   TEST_ASSERT_TRUE(ep.action==(i==1&&e>0&&e<4?1:0));
   TEST_ASSERT_TRUE(ep.approved==(i==1&&e>0&&e<4?1:0));
   TEST_ASSERT_TRUE(ep.source==(i==1&&e<4?(e==0?1:2):0));
  }
  uint8_t b[96];life.encode(b);TEST_ASSERT_TRUE(std::memcmp(b,golden[i],96)==0);
 }
 LifeEngine life;life.feedback(0,0,false,1);for(int n=0;n<3;++n)life.feedback(1,1,true,2);
 uint8_t b[96];life.encode(b);TEST_ASSERT_TRUE(std::memcmp(b,xuna_fixture::scripted,96)==0);
}
void choices() {
 // Exhaust every combination of evidence 0..3, plus near saturation, in both contexts.
 const uint16_t counts[]={0,1,2,3,999,1000,1001};
 for(uint8_t c=0;c<2;++c)for(auto p0:counts)for(auto n0:counts)for(auto p1:counts)for(auto n1:counts){
  LifeEngine life;life.positive[c][0]=p0;life.negative[c][0]=n0;
  life.positive[c][1]=p1;life.negative[c][1]=n1;
  // Independent rational comparison in 64 bits; ties are intentionally idle.
  const uint64_t left=uint64_t(p1+1)*(p0+n0+2);
  const uint64_t right=uint64_t(p0+1)*(p1+n1+2);
  TEST_ASSERT_TRUE(life.choose(c)==(left>right?1:0));TEST_ASSERT_TRUE(life.choose(1-c)==0);
 }
 LifeEngine life;TEST_ASSERT_TRUE(life.choose(2)==0);TEST_ASSERT_TRUE(life.choose(255)==0);
}
void parser() {
 // Explicit expected result table for each deployed opcode and all lengths 0..16.
 const uint8_t maxArg[]={0,3,1,1,0};
 for(uint8_t op=0;op<=7;++op)for(size_t len=0;len<=16;++len)
 for(unsigned arg=0;arg<=255;++arg){
  uint8_t b[16]={1,op,1,0,0,0,static_cast<uint8_t>(arg),0,42,0,0,0};
  if(op==3)b[7]=1;
  uint8_t expected=0;
  if(len!=8&&len!=12)expected=2;
  else if(op<1||op>4)expected=3;
  else if(len!=(op==3?12U:8U))expected=2;
  else if(arg>maxArg[op])expected=4;
  const auto parsed=parseCommand(b,len);TEST_ASSERT_TRUE(parsed.error==expected);
  TEST_ASSERT_TRUE(parsed.id==(len>=6?1U:0U));
 }
 uint8_t b[12]={1,3,0x78,0x56,0x34,0x12,1,2,42,0,0,0};
 auto c=parseCommand(b,12);TEST_ASSERT_TRUE(!c.error&&c.id==0x12345678&&c.target==42&&c.source==2);
 for(unsigned source=0;source<=255;++source){b[7]=source;TEST_ASSERT_TRUE(parseCommand(b,12).error==(source==1||source==2?0:4));}
 b[7]=2;b[8]=0;TEST_ASSERT_TRUE(parseCommand(b,12).error==4);b[8]=42;
 LifeEngine::put32(b+2,0);TEST_ASSERT_TRUE(parseCommand(b,12).error==4);
 b[0]=2;TEST_ASSERT_TRUE(parseCommand(b,12).error==1);TEST_ASSERT_TRUE(parseCommand(b,7).error==2);
 LifeEngine::put32(b+2,1);b[0]=1;
 for(uint8_t op: {uint8_t(1),uint8_t(2),uint8_t(4)}){
  b[1]=op;b[6]=0;b[7]=1;TEST_ASSERT_TRUE(parseCommand(b,8).error==4);
 }
 // Parser produces 0..4; adapter/cache result codes 5..7 are not parser errors.
}
void cache() {
 uint8_t b[12]={1,1,1,0,0,0,0,0};CommandCache cache;
 TEST_ASSERT_TRUE(cache.find(b,5)==-1);
 for(uint8_t result=0;result<=7;++result){cache.clear();cache.remember(b,8,result);TEST_ASSERT_TRUE(cache.find(b,8)==result);}
 cache.clear();cache.remember(b,8,0);b[6]=1;TEST_ASSERT_TRUE(cache.find(b,8)==6);b[6]=0;
 TEST_ASSERT_TRUE(cache.find(b,12)==6);cache.clear();TEST_ASSERT_TRUE(cache.find(b,8)==-1);
 cache.remember(b,5,0);cache.remember(b,13,0);TEST_ASSERT_TRUE(cache.find(b,8)==-1);
 for(uint32_t id=1;id<=17;++id){LifeEngine::put32(b+2,id);cache.remember(b,8,0);}
 LifeEngine::put32(b+2,1);TEST_ASSERT_TRUE(cache.find(b,8)==-1);
 for(uint32_t id=2;id<=17;++id){LifeEngine::put32(b+2,id);TEST_ASSERT_TRUE(cache.find(b,8)==0);}
 cache.clear();TEST_ASSERT_TRUE(cache.find(b,8)==-1);
 LifeEngine::put32(b+2,1);cache.remember(b,8,4);TEST_ASSERT_TRUE(cache.find(b,8)==4);
}
void recovery() {
 uint8_t a[96],b[96];LifeEngine first,second,out;
 first.generation=UINT32_MAX;first.activeSeconds=123;first.encode(a);
 second.generation=0;second.activeSeconds=456;second.encode(b);
 TEST_ASSERT_TRUE(out.recover(a,96,b,96)&&out.generation==0&&out.activeSeconds==456);
 TEST_ASSERT_TRUE(out.recover(b,96,a,96)&&out.generation==0&&out.activeSeconds==456);
 second.generation=UINT32_MAX;second.encode(b);
 TEST_ASSERT_TRUE(out.recover(a,96,b,96)&&out.activeSeconds==123); // equal generation: first wins
 b[10]^=1;TEST_ASSERT_TRUE(out.recover(a,96,b,96)&&out.activeSeconds==123);
 a[10]^=1;out.activeSeconds=789;TEST_ASSERT_TRUE(!out.recover(a,96,b,96)&&out.activeSeconds==789);
 TEST_ASSERT_TRUE(out.recover(a,0,xuna_fixture::fresh,96)&&out.eventCount==0);
}
void timing() {
 LifeEngine life;life.feedback(0,0,false,1);LocalBehavior routine;uint8_t action=99;
 const uint32_t start=UINT32_MAX-1000;routine.activity(start);
 TEST_ASSERT_TRUE(!routine.choose(start+59999U,true,life,action)&&action==99);
 TEST_ASSERT_TRUE(!routine.choose(start+60000U,false,life,action)&&action==99);
 TEST_ASSERT_TRUE(routine.choose(start+60000U,true,life,action)&&action==1);
 TEST_ASSERT_TRUE(!routine.choose(start+60001U,true,life,action));
 TEST_ASSERT_TRUE(routine.choose(start+120000U,true,life,action));
 routine.activity(500);TEST_ASSERT_TRUE(!routine.choose(60499,true,life,action));
 TEST_ASSERT_TRUE(routine.choose(60500,true,life,action));
 TEST_ASSERT_TRUE(routine.choose(1000000000,true,life,action)); // bounded late tick: one choice
 TEST_ASSERT_TRUE(!routine.choose(1000000000,true,life,action));
}
void s10_protocol_extensions() {
 const uint8_t sync[16]={1,5,0x78,0x56,0x34,0x12,1,0,
                         0x00,0xf1,0x53,0x65,0xa4,0x01,0,0};
 TEST_ASSERT_EQUAL_UINT8(2,parseCommand(sync,16).error);
 const BleCommand command=parseCommand(sync,16,false,true,false);
 TEST_ASSERT_EQUAL_UINT8(0,command.error);
 TEST_ASSERT_EQUAL_UINT32(0x12345678,command.id);
 TEST_ASSERT_EQUAL_UINT32(1700000000,command.utcSeconds);
 TEST_ASSERT_EQUAL_INT16(420,command.tzOffsetMin);
 uint8_t badSync[16]={1,5,1,0,0,0,1,1,1,0,0,0,0,0,0,0};
 TEST_ASSERT_EQUAL_UINT8(4,parseCommand(badSync,16,false,true,false).error);
 badSync[7]=0;badSync[12]=0x49;badSync[13]=0x03;
 TEST_ASSERT_EQUAL_UINT8(4,parseCommand(badSync,16,false,true,false).error);
 const uint8_t hatch[8]={1,7,9,0,0,0,0,0};
 TEST_ASSERT_EQUAL_UINT8(3,parseCommand(hatch,8).error);
 TEST_ASSERT_EQUAL_UINT8(0,parseCommand(hatch,8,false,false,true).error);
}
void s10_cache_and_lifecycle() {
 uint8_t sync[16]={1,5,9,0,0,0,1,0,1,0,0,0,0,0,0,0};
 CommandCache commandCache;commandCache.remember(sync,16,0);
 TEST_ASSERT_EQUAL_INT(0,commandCache.find(sync,16));
 sync[8]=2;TEST_ASSERT_EQUAL_INT(6,commandCache.find(sync,16));
 LifecycleProjection value;value.identity=IdentityWireState::Ready;
 value.stage=GrowthWireStage::Newborn;value.clock=ClockWireQuality::Synced;
 value.birth=BirthWireQuality::Synced;value.origin=IdentityWireOrigin::App;
 value.hasBornAt=true;uint8_t bytes[LifecycleCodec::kSize]={};
 TEST_ASSERT_TRUE(LifecycleCodec::encode(value,bytes));
 const uint8_t expected[8]={1,8,1,1,2,2,2,1};
 TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,bytes,8);
 value.stage=GrowthWireStage::Child;
 TEST_ASSERT_TRUE(LifecycleCodec::encode(value,bytes));
 TEST_ASSERT_EQUAL_UINT8(2,bytes[3]);
 value.stage=GrowthWireStage::Mature;
 TEST_ASSERT_TRUE(LifecycleCodec::encode(value,bytes));
 TEST_ASSERT_EQUAL_UINT8(3,bytes[3]);
 value.birth=BirthWireQuality::Unknown;
 TEST_ASSERT_FALSE(LifecycleCodec::encode(value,bytes));
}
void setUp() {}
void tearDown() {}
int main() {
 UNITY_BEGIN();
 RUN_TEST(snapshots); RUN_TEST(choices); RUN_TEST(parser);
 RUN_TEST(cache); RUN_TEST(recovery); RUN_TEST(timing);
 RUN_TEST(s10_protocol_extensions); RUN_TEST(s10_cache_and_lifecycle);
 return UNITY_END();
}
