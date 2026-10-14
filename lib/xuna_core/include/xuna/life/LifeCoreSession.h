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
// bounded event queue and ClockModel; transport and persistence remain outside
// xuna_core. The wrapper is suitable for Life Lab and later main-loop wiring.
class LifeCoreSession {
 public:
  static constexpr size_t kQueueCapacity = 8;

  explicit LifeCoreSession(uint64_t seed = 1, uint64_t stream = 3)
      : core_(seed, stream) {}

  bool begin(MonoMs now, bool restoreOk) {
    clock_.onBoot(now);
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
                  uint64_t mono) {
    return clock_.onSync(utcMs, tzOffsetMin, source, mono);
  }

  LifeCoreSessionStepOutput step(MonoMs now, size_t budget = kQueueCapacity) {
    LifeCoreEvent events[kQueueCapacity] = {};
    uint8_t count = 0;
    while (count < kQueueCapacity && count < budget && queue_.pop(events[count]))
      ++count;

    LifeCoreSessionStepOutput output;
    output.core = core_.step({now, events, count});
    output.clockQuality = clock_.quality(now);
    output.utcMinute = clock_.utcMinute(now);
    output.dayPart = clock_.dayPart(now);
    output.queueOverflow = queueOverflow_;
    queueOverflow_ = false;
    return output;
  }

  size_t queued() const { return queue_.size(); }
  const ClockModel& clock() const { return clock_; }
  const LifeCore& core() const { return core_; }
  LifeCore& core() { return core_; }

 private:
  LifeCore core_;
  ClockModel clock_;
  RingBuffer<LifeCoreEvent, kQueueCapacity> queue_;
  bool queueOverflow_ = false;
};

}  // namespace xuna
