#include <unity.h>
#include <cstring>
#include <xuna/base/LatencyHistogram.h>
#include <xuna/net/EchoProbe.h>
using namespace xuna;
typedef LatencyHistogram<101,1000> GapHistogram;  // 1 ms buckets, last = ≥ 100 ms
void histogram_empty_reports_zero(){
  GapHistogram h;
  TEST_ASSERT_EQUAL_UINT32(0,h.count());TEST_ASSERT_EQUAL_UINT32(0,h.percentileUs(500));
  TEST_ASSERT_EQUAL_UINT32(0,h.meanUs());TEST_ASSERT_EQUAL_UINT32(0,h.maxUs());
}
void histogram_percentiles_at_bucket_resolution(){
  GapHistogram h;
  for(int i=0;i<98;++i)h.add(10200);  // 10.2 ms loop passes
  h.add(42000);h.add(250000);         // one 42 ms stall, one 250 ms stall
  TEST_ASSERT_EQUAL_UINT32(100,h.count());
  TEST_ASSERT_EQUAL_UINT32(11000,h.percentileUs(500));  // bucket [10,11) ms
  TEST_ASSERT_EQUAL_UINT32(11000,h.percentileUs(980));
  TEST_ASSERT_EQUAL_UINT32(43000,h.percentileUs(990));  // 99th sample = 42 ms
  TEST_ASSERT_EQUAL_UINT32(250000,h.percentileUs(1000)); // overflow bucket → true max
  TEST_ASSERT_EQUAL_UINT32(250000,h.maxUs());
  TEST_ASSERT_EQUAL_UINT32((98u*10200u+42000u+250000u)/100u,h.meanUs());
}
void histogram_edge_never_exceeds_max_and_clears(){
  GapHistogram h;h.add(5);
  TEST_ASSERT_EQUAL_UINT32(5,h.percentileUs(990));
  h.clear();TEST_ASSERT_EQUAL_UINT32(0,h.count());TEST_ASSERT_EQUAL_UINT32(0,h.percentileUs(990));
}
void echo_frame_round_trip(){
  char frame[EchoProbe::kSize];EchoProbe::encode(4294967295u,frame);
  TEST_ASSERT_EQUAL_MEMORY("xuna.e14 seq=4294967295 ",frame,EchoProbe::kHeader);
  uint32_t seq=0;
  TEST_ASSERT_TRUE(EchoProbe::decode(frame,sizeof(frame),seq));TEST_ASSERT_EQUAL_UINT32(4294967295u,seq);
  EchoProbe::encode(7,frame);
  TEST_ASSERT_EQUAL_MEMORY("xuna.e14 seq=0000000007 ",frame,EchoProbe::kHeader);
  TEST_ASSERT_TRUE(EchoProbe::decode(frame,sizeof(frame),seq));TEST_ASSERT_EQUAL_UINT32(7,seq);
}
void echo_rejects_damaged_frames(){
  char frame[EchoProbe::kSize];uint32_t seq=99;
  EchoProbe::encode(12,frame);
  TEST_ASSERT_FALSE(EchoProbe::decode(frame,sizeof(frame)-1,seq));
  TEST_ASSERT_FALSE(EchoProbe::decode(nullptr,sizeof(frame),seq));
  frame[600]^=1;TEST_ASSERT_FALSE(EchoProbe::decode(frame,sizeof(frame),seq));
  EchoProbe::encode(12,frame);frame[15]='x';TEST_ASSERT_FALSE(EchoProbe::decode(frame,sizeof(frame),seq));
  EchoProbe::encode(12,frame);frame[0]='X';TEST_ASSERT_FALSE(EchoProbe::decode(frame,sizeof(frame),seq));
  EchoProbe::encode(12,frame);std::memcpy(frame+13,"4294967296",10);TEST_ASSERT_FALSE(EchoProbe::decode(frame,sizeof(frame),seq));
  TEST_ASSERT_EQUAL_UINT32(99,seq);  // untouched on failure
}
void probe_counts_ok_lost_bad_and_dropped(){
  EchoProbe p(5000);char out[EchoProbe::kSize];
  TEST_ASSERT_TRUE(p.due(1000));  // immediately after connect
  TEST_ASSERT_EQUAL_UINT32(1,p.begin(1000,out));
  TEST_ASSERT_FALSE(p.due(5999));
  p.received(out,sizeof(out),1040);
  TEST_ASSERT_EQUAL_UINT32(1,p.ok());TEST_ASSERT_EQUAL_UINT32(40,p.rttMaxMs());
  p.received(out,sizeof(out),1050);  // duplicate echo
  TEST_ASSERT_EQUAL_UINT32(1,p.bad());
  TEST_ASSERT_TRUE(p.due(6000));p.begin(6000,out);
  p.begin(11000,out);  // seq 2 never answered
  TEST_ASSERT_EQUAL_UINT32(1,p.lost());TEST_ASSERT_EQUAL_UINT32(1,p.lostInRow());
  char stale[EchoProbe::kSize];EchoProbe::encode(2,stale);
  p.received(stale,sizeof(stale),11010);  // late echo of the lost frame
  TEST_ASSERT_EQUAL_UINT32(2,p.bad());
  p.received(out,sizeof(out),11100);
  TEST_ASSERT_EQUAL_UINT32(2,p.ok());TEST_ASSERT_EQUAL_UINT32(0,p.lostInRow());
  TEST_ASSERT_EQUAL_UINT32(100,p.rttMaxMs());TEST_ASSERT_EQUAL_UINT32(70,p.rttMeanMs());
  p.begin(16000,out);p.disconnected();
  TEST_ASSERT_EQUAL_UINT32(1,p.dropped());TEST_ASSERT_EQUAL_UINT32(1,p.lost());
  TEST_ASSERT_TRUE(p.due(16001));  // reconnect sends at once
  TEST_ASSERT_EQUAL_UINT32(4,p.sent());
}
void probe_time_wraps(){
  EchoProbe p(5000);char out[EchoProbe::kSize];
  p.begin(0xFFFFF000u,out);
  TEST_ASSERT_FALSE(p.due(0xFFFFFFF0u));TEST_ASSERT_TRUE(p.due(0x00000400u));
  p.received(out,sizeof(out),0x00000010u);TEST_ASSERT_EQUAL_UINT32(0x1010u,p.rttMaxMs());
}
int main(){UNITY_BEGIN();RUN_TEST(histogram_empty_reports_zero);RUN_TEST(histogram_percentiles_at_bucket_resolution);
RUN_TEST(histogram_edge_never_exceeds_max_and_clears);RUN_TEST(echo_frame_round_trip);RUN_TEST(echo_rejects_damaged_frames);
RUN_TEST(probe_counts_ok_lost_bad_and_dropped);RUN_TEST(probe_time_wraps);return UNITY_END();}
