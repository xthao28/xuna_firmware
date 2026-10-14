#include <unity.h>

#include <xuna/life/LifeCoreSession.h>

using namespace xuna;

void session_drains_bounded_events_in_fifo_order() {
  LifeCoreSession session(42, 54);
  TEST_ASSERT_TRUE(session.begin(0, true));
  const LifeCoreEvent action{LifeCoreEventKind::RequestAction, 0, 0, 1, false,
                             0, 0};
  TEST_ASSERT_TRUE(session.enqueue(action));
  TEST_ASSERT_EQUAL_UINT32(1, session.queued());
  const LifeCoreSessionStepOutput output = session.step(10);
  TEST_ASSERT_EQUAL_UINT32(0, session.queued());
  TEST_ASSERT_TRUE(output.core.actionAccepted);
  TEST_ASSERT_FALSE(output.queueOverflow);
}

void session_reports_overflow_without_dropping_oldest_events() {
  LifeCoreSession session(42, 54);
  TEST_ASSERT_TRUE(session.begin(0, true));
  const LifeCoreEvent action{LifeCoreEventKind::RequestAction, 0, 0, 0, false,
                             0, 0};
  for (size_t i = 0; i < LifeCoreSession::kQueueCapacity; ++i)
    TEST_ASSERT_TRUE(session.enqueue(action));
  TEST_ASSERT_FALSE(session.enqueue(action));
  TEST_ASSERT_EQUAL_UINT32(LifeCoreSession::kQueueCapacity, session.queued());
  const LifeCoreSessionStepOutput output = session.step(10, 1);
  TEST_ASSERT_TRUE(output.queueOverflow);
  TEST_ASSERT_EQUAL_UINT32(LifeCoreSession::kQueueCapacity - 1,
                           session.queued());
}

void session_exposes_clock_quality_without_inventing_time() {
  LifeCoreSession session(42, 54);
  TEST_ASSERT_TRUE(session.begin(1000, true));
  const LifeCoreSessionStepOutput unknown = session.step(2000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ClockQuality::Unknown),
                          static_cast<uint8_t>(unknown.clockQuality));
  TEST_ASSERT_EQUAL_UINT32(0, unknown.utcMinute);

  const int64_t utc = ClockModel::kEpochUtcMs + 9 * 60000;
  const SyncResult sync = session.sync(utc, 420, TimeSource::Phone, 2000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SyncStatus::Accepted),
                          static_cast<uint8_t>(sync.status));
  const LifeCoreSessionStepOutput synced = session.step(3000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ClockQuality::Synced),
                          static_cast<uint8_t>(synced.clockQuality));
  TEST_ASSERT_EQUAL_UINT32(9, synced.utcMinute);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DayPart::Morning),
                          static_cast<uint8_t>(synced.dayPart));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(session_drains_bounded_events_in_fifo_order);
  RUN_TEST(session_reports_overflow_without_dropping_oldest_events);
  RUN_TEST(session_exposes_clock_quality_without_inventing_time);
  return UNITY_END();
}
