#include <unity.h>
#include <cstring>
#include <initializer_list>
#include <xuna/net/NetConfig.h>
using namespace xuna;
static NetConfig sample(){
  NetConfig c;std::strcpy(c.ssid,"Nhà Xuna");std::strcpy(c.pass,"secret-pass");
  std::strcpy(c.host,"192.168.1.20");c.port=8443;return c;
}
// Splits a message into frames of at most `chunk` payload bytes and feeds them.
static NetConfigCodec::Result feedAll(NetConfigCodec& codec,const uint8_t* m,size_t n,size_t chunk,uint8_t id=7){
  const uint8_t count=uint8_t((n+chunk-1)/chunk);
  NetConfigCodec::Result r=NetConfigCodec::Result::Rejected;
  for(uint8_t i=0;i<count;++i){
    uint8_t f[NetConfigCodec::kMaxFrame];f[0]=1;f[1]=id;f[2]=i;f[3]=count;
    const size_t len=(i+1==count)?n-size_t(i)*chunk:chunk;
    std::memcpy(f+4,m+size_t(i)*chunk,len);r=codec.feed(f,4+len);
    if(i+1<count)TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Partial),int(r));
  }
  return r;
}
void set_round_trip_in_one_and_many_frames(){
  uint8_t m[NetConfigCodec::kMaxMessage];const size_t n=NetConfigCodec::encodeSet(sample(),m,sizeof(m));
  TEST_ASSERT_EQUAL_UINT32(1+1+9+1+11+1+12+2,n);  // "Nhà" is 4 UTF-8 bytes
  for(size_t chunk:{size_t(16),size_t(5),n}){
    NetConfigCodec codec;
    TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Ready),int(feedAll(codec,m,n,chunk)));
    TEST_ASSERT_EQUAL(int(NetOp::Set),int(codec.op()));
    TEST_ASSERT_EQUAL_STRING("Nhà Xuna",codec.config().ssid);
    TEST_ASSERT_EQUAL_STRING("secret-pass",codec.config().pass);
    TEST_ASSERT_EQUAL_STRING("192.168.1.20",codec.config().host);
    TEST_ASSERT_EQUAL_UINT16(8443,codec.config().port);
    codec.clear();TEST_ASSERT_EQUAL(0,codec.config().pass[0]);TEST_ASSERT_FALSE(codec.config().valid());
  }
}
void forget_and_open_network(){
  NetConfigCodec codec;const uint8_t forget=2;
  TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Ready),int(feedAll(codec,&forget,1,16)));
  TEST_ASSERT_EQUAL(int(NetOp::Forget),int(codec.op()));
  NetConfig open=sample();open.pass[0]=0;  // open network: empty password
  uint8_t m[NetConfigCodec::kMaxMessage];const size_t n=NetConfigCodec::encodeSet(open,m,sizeof(m));
  TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Ready),int(feedAll(codec,m,n,64)));
  TEST_ASSERT_EQUAL_STRING("",codec.config().pass);
}
void rejects_malformed_messages(){
  uint8_t m[NetConfigCodec::kMaxMessage];NetOp op;NetConfig out;
  const size_t n=NetConfigCodec::encodeSet(sample(),m,sizeof(m));
  TEST_ASSERT_FALSE(NetConfigCodec::decode(m,n-1,op,out));           // truncated port
  uint8_t longer[NetConfigCodec::kMaxMessage+1];std::memcpy(longer,m,n);longer[n]=0;
  TEST_ASSERT_FALSE(NetConfigCodec::decode(longer,n+1,op,out));      // trailing byte
  const uint8_t forgetExtra[2]={2,0};TEST_ASSERT_FALSE(NetConfigCodec::decode(forgetExtra,2,op,out));
  const uint8_t unknown=9;TEST_ASSERT_FALSE(NetConfigCodec::decode(&unknown,1,op,out));
  NetConfig c=sample();
  c.port=0;size_t k=NetConfigCodec::encodeSet(c,m,sizeof(m));TEST_ASSERT_FALSE(NetConfigCodec::decode(m,k,op,out));
  c=sample();std::strcpy(c.pass,"short");k=NetConfigCodec::encodeSet(c,m,sizeof(m));
  TEST_ASSERT_FALSE(NetConfigCodec::decode(m,k,op,out));             // WPA passphrase < 8
  c=sample();std::strcpy(c.host,"bad host");k=NetConfigCodec::encodeSet(c,m,sizeof(m));
  TEST_ASSERT_FALSE(NetConfigCodec::decode(m,k,op,out));
  c=sample();c.ssid[2]='\t';k=NetConfigCodec::encodeSet(c,m,sizeof(m));
  TEST_ASSERT_FALSE(NetConfigCodec::decode(m,k,op,out));             // control byte
  c=sample();c.ssid[0]=0;k=NetConfigCodec::encodeSet(c,m,sizeof(m));
  TEST_ASSERT_FALSE(NetConfigCodec::decode(m,k,op,out));             // empty SSID
  const uint8_t ssid33[1+1+33]={1,33};TEST_ASSERT_FALSE(NetConfigCodec::decode(ssid33,sizeof(ssid33),op,out));
}
void rejects_bad_framing_and_wipes(){
  uint8_t m[NetConfigCodec::kMaxMessage];const size_t n=NetConfigCodec::encodeSet(sample(),m,sizeof(m));
  NetConfigCodec codec;
  uint8_t f[NetConfigCodec::kMaxFrame];f[0]=1;f[1]=3;f[2]=1;f[3]=2;std::memcpy(f+4,m,10);
  TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Rejected),int(codec.feed(f,14)));  // starts mid-message
  f[2]=0;f[0]=2;TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Rejected),int(codec.feed(f,14)));  // version
  f[0]=1;f[3]=0;TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Rejected),int(codec.feed(f,14)));  // no chunks
  f[3]=2;TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Partial),int(codec.feed(f,14)));
  f[1]=4;f[2]=1;TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Rejected),int(codec.feed(f,14)));  // other id
  TEST_ASSERT_EQUAL(int(NetOp::None),int(codec.op()));
  // A message longer than the maximum is refused before it completes.
  uint8_t big[NetConfigCodec::kMaxMessage+10]={};big[0]=1;
  TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Rejected),int(feedAll(codec,big,sizeof(big),100)));
  // An invalid whole message leaves nothing behind.
  m[n-1]=0;m[n-2]=0;  // port 0
  TEST_ASSERT_EQUAL(int(NetConfigCodec::Result::Rejected),int(feedAll(codec,m,n,64)));
  TEST_ASSERT_EQUAL(0,codec.config().pass[0]);
}
void status_projection_layout(){
  NetStatus s;s.wifi=WifiState::Connected;s.cloud=CloudState::Ready;s.rssi=-61;
  s.lastError=NetError::Replaced;s.lastBackupGeneration=0x01020304;
  uint8_t b[NetStatus::kSize];s.encode(b);
  const uint8_t expected[NetStatus::kSize]={1,2,3,uint8_t(-61),11,4,3,2,1};
  TEST_ASSERT_EQUAL_MEMORY(expected,b,sizeof(b));
}
int main(){UNITY_BEGIN();RUN_TEST(set_round_trip_in_one_and_many_frames);RUN_TEST(forget_and_open_network);
RUN_TEST(rejects_malformed_messages);RUN_TEST(rejects_bad_framing_and_wipes);RUN_TEST(status_projection_layout);
return UNITY_END();}
