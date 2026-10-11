#include <cassert>
#include <iostream>
#include "LifeEngine.h"
#include "BleProtocol.h"
#include "LocalBehavior.h"
int main(){
  using namespace xuna;LifeEngine a;
  assert(a.choose(0)==0);a.feedback(0,0,false,1);assert(a.choose(0)==1);assert(a.choose(1)==0);
  for(int i=0;i<20;++i)a.feedback(0,1,true,2);
  assert(a.choose(0)==1&&a.eventCount==21&&a.episodeCount==8);
  uint8_t bytes[LifeEngine::kSnapshotSize];a.encode(bytes);LifeEngine b;assert(b.decode(bytes,sizeof(bytes)));assert(b.choose(0)==1&&b.eventCount==21);
  bytes[20]^=1;assert(!b.decode(bytes,sizeof(bytes)));assert(b.eventCount==21);
  uint8_t cmd[8]={1,1,0x78,0x56,0x34,0x12,1,0};auto c=parseCommand(cmd,8);assert(!c.error&&c.id==0x12345678);
  CommandCache cache;assert(cache.find(cmd,8)==-1);cache.remember(cmd,8,0);assert(cache.find(cmd,8)==0);cmd[6]=0;assert(cache.find(cmd,8)==6);cache.clear();assert(cache.find(cmd,8)==-1);
  assert(parseCommand(cmd,7).error==2);cmd[0]=2;assert(parseCommand(cmd,8).error==1);
  LifeEngine saturated;for(int i=0;i<10000;++i)saturated.feedback(1,1,true,1);assert(saturated.positive[1][1]<=1001);assert(saturated.choose(1)==1);
  uint8_t oldSlot[LifeEngine::kSnapshotSize],newSlot[LifeEngine::kSnapshotSize];
  a.generation=10;a.encode(oldSlot);a.generation=11;a.encode(newSlot);
  LifeEngine recovered;assert(recovered.recover(oldSlot,sizeof(oldSlot),newSlot,sizeof(newSlot)));assert(recovered.generation==11);
  newSlot[10]^=1;assert(recovered.recover(oldSlot,sizeof(oldSlot),newSlot,sizeof(newSlot)));assert(recovered.generation==10);
  oldSlot[10]^=1;assert(!recovered.recover(oldSlot,sizeof(oldSlot),newSlot,sizeof(newSlot)));
  assert(!recovered.recover(oldSlot,20,newSlot,0));
  uint8_t feedback[12]={1,3,1,0,0,0,1,2,42,0,0,0};assert(!parseCommand(feedback,12).error);feedback[8]=0;assert(parseCommand(feedback,12).error==4);
  LocalBehavior behavior;behavior.activity(UINT32_MAX-1000);uint8_t action=0;
  assert(!behavior.choose(58998,true,a,action));assert(behavior.choose(58999,true,a,action));assert(action==1);
  assert(!behavior.choose(999999,false,a,action));
  std::cout<<"PASS: measured preference adaptation, context isolation, bounded episodes/counters, snapshot roundtrip/corruption, command validation/dedup\n";
}
