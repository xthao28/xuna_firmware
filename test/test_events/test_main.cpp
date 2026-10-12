#include <unity.h>
#include <xuna/event/EventQueue.h>
using namespace xuna;
void registry() {
 TEST_ASSERT_EQUAL_UINT8(0x30,static_cast<uint8_t>(EventType::FeedbackApplied));
 TEST_ASSERT_TRUE(eventInfo(EventType::FeedbackApplied)->critical);
 TEST_ASSERT_TRUE(eventInfo(EventType::FactConfirmed)->critical);
 TEST_ASSERT_EQUAL(EventCategory::Evolution,eventInfo(EventType::TraitUnlocked)->category);
 TEST_ASSERT_EQUAL(PersistClass::Aggregate,eventInfo(EventType::ButtonInteraction)->persistence);
 TEST_ASSERT_NULL(eventInfo(static_cast<EventType>(0x90)));
 for(size_t i=0;i<kEventTypeCount;++i)for(size_t j=i+1;j<kEventTypeCount;++j)
  TEST_ASSERT_NOT_EQUAL(kEventTypes[i].type,kEventTypes[j].type);
}
void ring() {
 RingBuffer<int,3> ring;int value=-1;TEST_ASSERT_FALSE(ring.pop(value));TEST_ASSERT_EQUAL_INT(-1,value);
 TEST_ASSERT_TRUE(ring.push(1));TEST_ASSERT_TRUE(ring.push(2));TEST_ASSERT_TRUE(ring.push(3));
 TEST_ASSERT_FALSE(ring.push(4));TEST_ASSERT_EQUAL_UINT32(3,ring.size());
 TEST_ASSERT_TRUE(ring.pop(value));TEST_ASSERT_EQUAL_INT(1,value);TEST_ASSERT_TRUE(ring.push(4));
 for(int i=2;i<=4;++i){TEST_ASSERT_TRUE(ring.pop(value));TEST_ASSERT_EQUAL_INT(i,value);}
 TEST_ASSERT_TRUE(ring.empty());ring.push(5);ring.clear();TEST_ASSERT_TRUE(ring.empty());
 RingBuffer<int,1> single;TEST_ASSERT_TRUE(single.push(7));TEST_ASSERT_FALSE(single.push(8));
 TEST_ASSERT_TRUE(single.pop(value));TEST_ASSERT_EQUAL_INT(7,value);
}
void ids() {
 EventIdGenerator first(12),second(13);uint64_t id=0;
 TEST_ASSERT_TRUE(first.next(id));TEST_ASSERT_EQUAL_UINT64((uint64_t(12)<<32)|1,id);
 TEST_ASSERT_TRUE(first.next(id));TEST_ASSERT_EQUAL_UINT64((uint64_t(12)<<32)|2,id);
 TEST_ASSERT_TRUE(second.next(id));TEST_ASSERT_EQUAL_UINT64((uint64_t(13)<<32)|1,id);
 EventIdGenerator exhausted(12,UINT32_MAX-1);TEST_ASSERT_TRUE(exhausted.next(id));
 TEST_ASSERT_EQUAL_UINT64((uint64_t(12)<<32)|UINT32_MAX,id);
 const uint64_t previous=id;TEST_ASSERT_FALSE(exhausted.next(id));TEST_ASSERT_EQUAL_UINT64(previous,id);
}
void queue() {
 EventQueue<2> queue;Event event;event.type=EventType::ButtonInteraction;event.eventId=1;
 TEST_ASSERT_EQUAL(PushResult::Queued,queue.push(event));event.eventId=2;
 TEST_ASSERT_EQUAL(PushResult::Queued,queue.push(event));event.eventId=3;
 TEST_ASSERT_EQUAL(PushResult::Dropped,queue.push(event));TEST_ASSERT_EQUAL_UINT32(1,queue.dropped());
 const EventType critical[]={EventType::FeedbackApplied,EventType::FactConfirmed,EventType::FactSuperseded,EventType::FactDeleted,EventType::MemoryForgotten};
 for(auto type:critical){event.type=type;TEST_ASSERT_EQUAL(PushResult::RejectedFull,queue.push(event));}
 TEST_ASSERT_EQUAL_UINT32(5,queue.rejected());TEST_ASSERT_EQUAL_UINT32(1,queue.dropped());
 event.type=static_cast<EventType>(0x90);TEST_ASSERT_EQUAL(PushResult::RejectedType,queue.push(event));
 uint64_t seen[3]={};size_t count=0;
 auto sink=[&](const Event& e){seen[count++]=e.eventId;};
 TEST_ASSERT_EQUAL_UINT32(0,queue.drain(sink,0));TEST_ASSERT_EQUAL_UINT32(0,count);
 TEST_ASSERT_EQUAL_UINT32(1,queue.drain(sink,1));TEST_ASSERT_EQUAL_UINT64(1,seen[0]);
 TEST_ASSERT_EQUAL_UINT32(1,queue.size());TEST_ASSERT_EQUAL_UINT32(1,queue.drain(sink,10));
 TEST_ASSERT_EQUAL_UINT64(2,seen[1]);TEST_ASSERT_EQUAL_UINT32(0,queue.drain(sink,10));
}
void drain_bound() {
 EventQueue<3> queue;Event event;event.type=EventType::BootStarted;queue.push(event);
 unsigned calls=0;auto sink=[&](const Event& e){++calls;queue.push(e);};
 TEST_ASSERT_EQUAL_UINT32(2,queue.drain(sink,2));TEST_ASSERT_EQUAL_UINT32(2,calls);
 TEST_ASSERT_EQUAL_UINT32(1,queue.size()); // callback enqueues cannot bypass work budget
}
void envelope() {
 Event event;TEST_ASSERT_EQUAL_UINT64(0,event.eventId);TEST_ASSERT_EQUAL_UINT32(0,event.utcMinute);
 TEST_ASSERT_EQUAL(ClockQuality::Unknown,event.clockQuality);
 for(auto byte:event.payload)TEST_ASSERT_EQUAL_UINT8(0,byte);
 TEST_ASSERT_TRUE(sizeof(Event)<=48); // alignment allowed; not the persisted 40-byte codec
}
void setUp() {} void tearDown() {}
int main(){UNITY_BEGIN();RUN_TEST(registry);RUN_TEST(ring);RUN_TEST(ids);RUN_TEST(queue);RUN_TEST(drain_bound);RUN_TEST(envelope);return UNITY_END();}
