#include <unity.h>
#include <xuna/LifeEngine.h>
#include <xuna/BleProtocol.h>
#include <xuna/LocalBehavior.h>
void test_life_baseline(){
  using namespace xuna;LifeEngine a;
  TEST_ASSERT_TRUE(a.choose(0)==0);a.feedback(0,0,false,1);TEST_ASSERT_TRUE(a.choose(0)==1);TEST_ASSERT_TRUE(a.choose(1)==0);
  for(int i=0;i<20;++i)a.feedback(0,1,true,2);
  TEST_ASSERT_TRUE(a.choose(0)==1&&a.eventCount==21&&a.episodeCount==8);
  uint8_t bytes[LifeEngine::kSnapshotSize];a.encode(bytes);LifeEngine b;TEST_ASSERT_TRUE(b.decode(bytes,sizeof(bytes)));TEST_ASSERT_TRUE(b.choose(0)==1&&b.eventCount==21);
  bytes[20]^=1;TEST_ASSERT_TRUE(!b.decode(bytes,sizeof(bytes)));TEST_ASSERT_TRUE(b.eventCount==21);
  uint8_t cmd[8]={1,1,0x78,0x56,0x34,0x12,1,0};auto c=parseCommand(cmd,8);TEST_ASSERT_TRUE(!c.error&&c.id==0x12345678);
  CommandCache cache;TEST_ASSERT_TRUE(cache.find(cmd,8)==-1);cache.remember(cmd,8,0);TEST_ASSERT_TRUE(cache.find(cmd,8)==0);cmd[6]=0;TEST_ASSERT_TRUE(cache.find(cmd,8)==6);cache.clear();TEST_ASSERT_TRUE(cache.find(cmd,8)==-1);
  TEST_ASSERT_TRUE(parseCommand(cmd,7).error==2);cmd[0]=2;TEST_ASSERT_TRUE(parseCommand(cmd,8).error==1);
  LifeEngine saturated;for(int i=0;i<10000;++i)saturated.feedback(1,1,true,1);TEST_ASSERT_TRUE(saturated.positive[1][1]<=1001);TEST_ASSERT_TRUE(saturated.choose(1)==1);
  uint8_t oldSlot[LifeEngine::kSnapshotSize],newSlot[LifeEngine::kSnapshotSize];
  a.generation=10;a.encode(oldSlot);a.generation=11;a.encode(newSlot);
  LifeEngine recovered;TEST_ASSERT_TRUE(recovered.recover(oldSlot,sizeof(oldSlot),newSlot,sizeof(newSlot)));TEST_ASSERT_TRUE(recovered.generation==11);
  newSlot[10]^=1;TEST_ASSERT_TRUE(recovered.recover(oldSlot,sizeof(oldSlot),newSlot,sizeof(newSlot)));TEST_ASSERT_TRUE(recovered.generation==10);
  oldSlot[10]^=1;TEST_ASSERT_TRUE(!recovered.recover(oldSlot,sizeof(oldSlot),newSlot,sizeof(newSlot)));
  TEST_ASSERT_TRUE(!recovered.recover(oldSlot,20,newSlot,0));
  uint8_t feedback[12]={1,3,1,0,0,0,1,2,42,0,0,0};TEST_ASSERT_TRUE(!parseCommand(feedback,12).error);feedback[8]=0;TEST_ASSERT_TRUE(parseCommand(feedback,12).error==4);
  LocalBehavior behavior;behavior.activity(UINT32_MAX-1000);uint8_t action=0;
  TEST_ASSERT_TRUE(!behavior.choose(58998,true,a,action));TEST_ASSERT_TRUE(behavior.choose(58999,true,a,action));TEST_ASSERT_TRUE(action==1);
  TEST_ASSERT_TRUE(!behavior.choose(999999,false,a,action));
}

void setUp() {}
void tearDown() {}
int main() { UNITY_BEGIN(); RUN_TEST(test_life_baseline); return UNITY_END(); }
