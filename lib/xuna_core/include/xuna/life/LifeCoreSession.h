#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/base/RingBuffer.h>
#include <xuna/life/LifeCore.h>
#include <xuna/time/ClockModel.h>

namespace xuna {

struct LifeCoreSessionStepOutput {
  LifeCoreStepOutput core;
  ClockQuality clockQuality = ClockQuality::Unknown;
  uint32_t utcMinute = 0;
  DayPart dayPart = DayPart::Unknown;
  bool queueOverflow = false;
};

// Adapter-free session wrapper for deterministic replay. It owns only a
// bounded event queue; the clock lives in LifeCore (S10 host part) so the
// learner can use day parts. Transport and persistence remain outside
// xuna_core. Suitable for Life Lab and later main-loop wiring.
class LifeCoreSession {
 public:
  static constexpr size_t kQueueCapacity = 8;

  explicit LifeCoreSession(uint64_t seed = 1, uint64_t stream = 3)
      : core_(seed, stream) {}

  bool begin(MonoMs now, bool restoreOk) {
    queue_.clear();
    queueOverflow_ = false;
    return core_.begin(now, restoreOk);
  }

  bool enqueue(const LifeCoreEvent& event) {
    if (queue_.push(event)) return true;
    queueOverflow_ = true;
    return false;
  }

  SyncResult sync(int64_t utcMs, int16_t tzOffsetMin, TimeSource source,
                  MonoMs now) {
    return core_.sync(utcMs, tzOffsetMin, source, now);
  }

  LifeCoreSessionStepOutput step(MonoMs now, size_t budget = kQueueCapacity) {
    LifeCoreEvent events[kQueueCapacity] = {};
    uint8_t count = 0;
    while (count < kQueueCapacity && count < budget && queue_.pop(events[count]))
      ++count;

    LifeCoreSessionStepOutput output;
    output.core = core_.step({now, events, count});
    const ClockModel& clock = core_.clock();
    const uint64_t mono = core_.mono64();
    output.clockQuality = clock.quality(mono);
    output.utcMinute = clock.utcMinute(mono);
    output.dayPart = clock.dayPart(mono);
    output.queueOverflow = queueOverflow_;
    queueOverflow_ = false;
    return output;
  }

  size_t queued() const { return queue_.size(); }
  const ClockModel& clock() const { return core_.clock(); }
  const LifeCore& core() const { return core_; }
  LifeCore& core() { return core_; }

 private:
  LifeCore core_;
  RingBuffer<LifeCoreEvent, kQueueCapacity> queue_;
  bool queueOverflow_ = false;
};

}  // namespace xuna
