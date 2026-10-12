#pragma once
#include <xuna/base/RingBuffer.h>
#include <xuna/event/Event.h>
namespace xuna {
enum class PushResult : uint8_t { Queued,Dropped,RejectedFull,RejectedType };
// Single-owner queue; ISR/BLE producers need a later transport adapter, not direct calls.
template<size_t N=32> class EventQueue {
 public:
  PushResult push(const Event& event) {
    const auto* info=eventInfo(event.type);if(!info)return PushResult::RejectedType;
    if(buffer_.push(event))return PushResult::Queued;
    if(info->critical){increment(rejected_);return PushResult::RejectedFull;}
    increment(dropped_);return PushResult::Dropped;
  }
  // Sink consumes synchronously. This queue is not a commit/ACK or persistence mechanism.
  template<class Sink> size_t drain(Sink&& sink,size_t budget) {
    size_t count=0;Event event;
    while(count<budget&&buffer_.pop(event)){++count;sink(event);}
    return count;
  }
  size_t size() const { return buffer_.size(); }
  uint32_t dropped() const { return dropped_; }
  uint32_t rejected() const { return rejected_; }
 private:
  static void increment(uint32_t& counter){if(counter!=UINT32_MAX)++counter;}
  RingBuffer<Event,N> buffer_;uint32_t dropped_=0,rejected_=0;
};
}
