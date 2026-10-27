#include <unity.h>
#include <cstring>
#include <xuna/net/ExportWriter.h>
using namespace xuna;
// BLE bytes behind backend/tests/fixtures/xuna_export_v1_golden.json (Dart-produced).
static const uint8_t kLife[20]={1,20,0,1, 0xfa,0x1e,0,0, 5,0,0,0, 2,0,0,0, 1,0,1,5};
static const uint8_t kPage0[20]={1,2,0,0, 0,1,1,1, 1,0,1,2};
static const uint8_t kPage1[20]={1,0,1,0};
static const uint8_t kExplanation[20]={1,20,0,3,1,1,0,3, 203,0, 0xcd,2, 0xcd,2, 0,0, 0,0, 0,0};
static const uint8_t kPet[16]={0xd9,0x45,0xc7,0x70,0x26,0xfd,0x4f,0x4a,0x8a,0x89,0x9b,0xbf,0xf4,0xae,0xed,0x86};
static const char kGolden[]=
  "{\"exportedAtUtc\":\"2026-10-17T14:27:22.000Z\",\"app\":{\"version\":\"1.0.0+1\"},\"capabilities\":{\"statedPreference\":true,"
  "\"timeSync\":true,\"identity\":true},\"identity\":{\"petId\":\"d945c77026fd4f4a8a899bbff4aeed86\",\"origin\":\"migrated_from_v1\","
  "\"snapshotGeneration\":38},\"life\":{\"activeSeconds\":7930,\"feedbackCount\":5,\"decisionId\":2,\"context\":\"quiet\","
  "\"action\":\"happy\",\"quietChoice\":\"happy\",\"playChoice\":\"idle\",\"canFeedback\":true,\"memoryCount\":5},\"memories\":"
  "[{\"context\":\"quiet\",\"action\":\"happy\",\"approved\":true,\"source\":\"button\"},{\"context\":\"play\",\"action\":\"idle\","
  "\"approved\":true,\"source\":\"text\"}],\"explanation\":{\"interaction\":\"quiet\",\"dayPart\":\"evening\",\"action\":\"happy\","
  "\"reason\":\"highest_estimate\",\"fine\":false,\"rated\":3,\"probabilityQ8\":203,\"effectiveMassQ8\":717,\"positiveQ8\":717,"
  "\"negativeQ8\":0,\"statedPositiveQ8\":0,\"statedNegativeQ8\":0}}";
static ExportSource golden(){
  ExportSource s;s.life=kLife;s.memoryPages[0]=kPage0;s.memoryPages[1]=kPage1;s.explanation=kExplanation;
  s.petId=kPet;s.origin=3;s.generation=38;s.exportedAtUtcMs=1792247242000LL;s.version="1.0.0+1";return s;
}
void payload_matches_the_dart_golden(){
  static char payload[ExportWriter::kMaxPayload];
  const size_t n=ExportWriter::payload(golden(),payload,sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(std::strlen(kGolden),n);
  TEST_ASSERT_EQUAL_STRING(kGolden,payload);
  static char envelope[ExportWriter::kMaxEnvelope];char hash[65];
  const size_t e=ExportWriter::envelope(payload,n,envelope,sizeof(envelope),hash);
  TEST_ASSERT_EQUAL_STRING("fede139d26cc3aef0377d9c37f20018d19d908efb001b4d427f7b6544258c1f8",hash);
  TEST_ASSERT_TRUE(e>n);
  const char head[]="{\"format\":\"xuna.export.v1\",\"sha256\":\"fede139d";
  TEST_ASSERT_EQUAL_MEMORY(head,envelope,sizeof(head)-1);
  static char frame[ExportWriter::kMaxFrame];
  const size_t f=ExportWriter::projectionFrame(38,envelope,e,frame,sizeof(frame));
  TEST_ASSERT_TRUE(f>e);
  const char frameHead[]="{\"type\":\"projection\",\"schemaVersion\":1,\"generation\":38,\"export\":{\"format\"";
  TEST_ASSERT_EQUAL_MEMORY(frameHead,frame,sizeof(frameHead)-1);
}
void largest_payload_fits_and_out_of_range_bytes_refused(){
  static char payload[ExportWriter::kMaxPayload];
  uint8_t life[20];std::memcpy(life,kLife,20);for(int i=4;i<16;++i)life[i]=0xff;life[19]=8;
  uint8_t full[20]={1,4,0,0};for(int i=0;i<4;++i){full[4+4*i]=2;full[5+4*i]=3;full[6+4*i]=0;full[7+4*i]=3;}
  uint8_t full1[20];std::memcpy(full1,full,20);full1[2]=1;
  uint8_t x[20];std::memcpy(x,kExplanation,20);x[8]=0;x[9]=1;for(int i=10;i<20;i+=2){x[i]=0xff;x[i+1]=0xff;}
  ExportSource s=golden();s.life=life;s.memoryPages[0]=full;s.memoryPages[1]=full1;s.explanation=x;
  s.generation=0xffffffffu;s.version="firmware/0.2.0-mvp";
  TEST_ASSERT_TRUE(ExportWriter::payload(s,payload,sizeof(payload))>0);
  uint8_t bad[20];
  struct Case { const uint8_t* src;int index;uint8_t value;int which; } cases[]={
    {kLife,3,4,0},{kLife,19,9,0},{kLife,2,2,0},{kPage0,4,3,1},{kPage0,7,0,1},{kPage0,7,4,1},{kPage0,1,5,1},
    {kExplanation,4,3,2},{kExplanation,9,2,2}};
  for(const Case& c:cases){
    std::memcpy(bad,c.src,20);bad[c.index]=c.value;
    ExportSource t=golden();
    if(c.which==0)t.life=bad;else if(c.which==1)t.memoryPages[0]=bad;else t.explanation=bad;
    TEST_ASSERT_EQUAL_UINT32(0,ExportWriter::payload(t,payload,sizeof(payload)));
  }
  ExportSource t=golden();t.origin=4;TEST_ASSERT_EQUAL_UINT32(0,ExportWriter::payload(t,payload,sizeof(payload)));
  TEST_ASSERT_EQUAL_UINT32(0,ExportWriter::payload(golden(),payload,100));  // never a partial payload
}
void time_and_null_explanation(){
  char t[32];
  ExportWriter::time(0,t);TEST_ASSERT_EQUAL_STRING("unknown",t);
  ExportWriter::time(951782400123LL,t);TEST_ASSERT_EQUAL_STRING("2000-02-29T00:00:00.123Z",t);
  ExportWriter::time(4102444799999LL,t);TEST_ASSERT_EQUAL_STRING("2099-12-31T23:59:59.999Z",t);
  static char payload[ExportWriter::kMaxPayload];
  ExportSource s=golden();s.explanation=nullptr;s.exportedAtUtcMs=0;
  TEST_ASSERT_TRUE(ExportWriter::payload(s,payload,sizeof(payload))>0);
  TEST_ASSERT_NOT_NULL(std::strstr(payload,"\"explanation\":null}"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload,"\"exportedAtUtc\":\"unknown\""));
}
int main(){UNITY_BEGIN();RUN_TEST(payload_matches_the_dart_golden);RUN_TEST(largest_payload_fits_and_out_of_range_bytes_refused);
RUN_TEST(time_and_null_explanation);return UNITY_END();}
