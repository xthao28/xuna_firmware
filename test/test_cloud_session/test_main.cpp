#include <unity.h>
#include <cstring>
#include <xuna/base/Base64.h>
#include <xuna/identity/DeviceProof.h>
#include <xuna/net/CloudSession.h>
using namespace xuna;
static const char kPet[]="d945c77026fd4f4a8a899bbff4aeed86";
void base64_vectors(){
  char out[16];
  const char* inputs[]={"","f","fo","foo","foob","fooba","foobar"};
  const char* expected[]={"","Zg==","Zm8=","Zm9v","Zm9vYg==","Zm9vYmE=","Zm9vYmFy"};  // RFC 4648 §10
  for(int i=0;i<7;++i){
    const size_t n=std::strlen(inputs[i]);
    TEST_ASSERT_TRUE(Base64::encode(reinterpret_cast<const uint8_t*>(inputs[i]),n,out,sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(expected[i],out);
    uint8_t back[8]={};
    TEST_ASSERT_TRUE(Base64::decode(expected[i],std::strlen(expected[i]),back,n));
    if(n)TEST_ASSERT_EQUAL_MEMORY(inputs[i],back,n);  // Unity refuses empty compares
  }
  uint8_t raw[32];
  TEST_ASSERT_FALSE(Base64::decode("Zm9v",4,raw,2));      // wrong expected size
  TEST_ASSERT_FALSE(Base64::decode("Zm9",3,raw,2));       // not a multiple of 4
  TEST_ASSERT_FALSE(Base64::decode("Zm!v",4,raw,3));      // bad character
  TEST_ASSERT_FALSE(Base64::decode("Zg=v",4,raw,1));      // padding in the middle
  TEST_ASSERT_FALSE(Base64::encode(raw,3,out,4));         // no room for NUL
}
void json_flat_lookups(){
  const char j[]="{ \"type\" : \"ready\", \"schemaVersion\":1,\"heartbeatS\": 30, \"esc\":\"a\\\"b\"}";
  char s[16];uint32_t v=0;const size_t n=sizeof(j)-1;
  TEST_ASSERT_TRUE(JsonFlat::string(j,n,"type",s,sizeof(s)));TEST_ASSERT_EQUAL_STRING("ready",s);
  TEST_ASSERT_TRUE(JsonFlat::uint(j,n,"heartbeatS",v));TEST_ASSERT_EQUAL_UINT32(30,v);
  TEST_ASSERT_FALSE(JsonFlat::string(j,n,"esc",s,sizeof(s)));     // escapes are not needed, refused
  TEST_ASSERT_FALSE(JsonFlat::string(j,n,"heartbeatS",s,sizeof(s)));
  TEST_ASSERT_FALSE(JsonFlat::uint(j,n,"type",v));
  TEST_ASSERT_FALSE(JsonFlat::uint(j,n,"missing",v));
  TEST_ASSERT_FALSE(JsonFlat::string(j,n,"type",s,3));            // too long for the buffer
  const char big[]="{\"seq\":4294967296}";
  TEST_ASSERT_FALSE(JsonFlat::uint(big,sizeof(big)-1,"seq",v));
}
// Runs hello → challenge → proof → ready; returns the session.
static CloudSession readySession(uint32_t now){
  CloudSession s(kPet,"0.2.0-mvp");char out[CloudSession::kMaxFrame];size_t len=0;
  s.connecting();TEST_ASSERT_EQUAL(int(CloudState::Connecting),int(s.state()));
  const size_t hello=s.opened(out,sizeof(out),now);
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"hello\",\"schemaVersion\":1,\"petId\":\"d945c77026fd4f4a8a899bbff4aeed86\","
                           "\"firmware\":\"0.2.0-mvp\",\"capabilities\":[]}",out);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(out),hello);
  TEST_ASSERT_EQUAL(int(CloudState::Proving),int(s.state()));
  const char challenge[]="{\"type\":\"challenge\",\"schemaVersion\":1,"
                         "\"nonce\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\",\"expiresIn\":60}";
  TEST_ASSERT_EQUAL(int(CloudSession::Action::Sign),int(s.received(challenge,sizeof(challenge)-1,out,sizeof(out),len,now)));
  for(int i=0;i<32;++i)TEST_ASSERT_EQUAL_UINT8(i,s.nonce()[i]);
  uint8_t sig[64];for(int i=0;i<64;++i)sig[i]=uint8_t(i);
  const size_t proof=s.signedProof(40,sig,out,sizeof(out));
  TEST_ASSERT_TRUE(proof>0);
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"proof\",\"schemaVersion\":1,\"generation\":40,\"signature\":"
                           "\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+Pw==\"}",out);
  TEST_ASSERT_EQUAL_UINT32(0,s.signedProof(40,sig,out,sizeof(out)));  // only once
  const char ready[]="{\"type\":\"ready\",\"schemaVersion\":1,\"serverTime\":\"2026-10-27T12:00:00+00:00\",\"heartbeatS\":30}";
  TEST_ASSERT_EQUAL(int(CloudSession::Action::Ready),int(s.received(ready,sizeof(ready)-1,out,sizeof(out),len,now)));
  TEST_ASSERT_EQUAL(int(CloudState::Ready),int(s.state()));
  TEST_ASSERT_EQUAL_UINT32(30000,s.heartbeatMs());
  return s;
}
void handshake_and_heartbeat(){
  CloudSession s=readySession(1000);char out[CloudSession::kMaxFrame];size_t len=0;
  TEST_ASSERT_EQUAL(int(CloudSession::Action::None),int(s.tick(30999,out,sizeof(out),len)));
  TEST_ASSERT_EQUAL(int(CloudSession::Action::Send),int(s.tick(31000,out,sizeof(out),len)));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"ping\",\"schemaVersion\":1,\"seq\":1}",out);TEST_ASSERT_EQUAL_UINT32(std::strlen(out),len);
  const char pong[]="{\"type\":\"pong\",\"schemaVersion\":1,\"seq\":1}";
  TEST_ASSERT_EQUAL(int(CloudSession::Action::None),int(s.received(pong,sizeof(pong)-1,out,sizeof(out),len,31050)));
  const char future[]="{\"type\":\"suggestion\",\"schemaVersion\":1,\"x\":2}";
  TEST_ASSERT_EQUAL(int(CloudSession::Action::None),int(s.received(future,sizeof(future)-1,out,sizeof(out),len,31060)));
  TEST_ASSERT_EQUAL(int(CloudState::Ready),int(s.state()));  // ignored, never acted on
  // No pong for three heartbeats: the link is dead.
  s.tick(61050,out,sizeof(out),len);s.tick(91050,out,sizeof(out),len);
  TEST_ASSERT_EQUAL(int(CloudSession::Action::Fail),int(s.tick(121051,out,sizeof(out),len)));
  TEST_ASSERT_EQUAL(int(NetError::ServerUnreachable),int(s.lastError()));
}
void protocol_violations_fail(){
  char out[CloudSession::kMaxFrame];size_t len=0;
  const char* bad[]={
    "{\"type\":\"ready\",\"schemaVersion\":1,\"heartbeatS\":30}",                 // ready before challenge
    "{\"type\":\"challenge\",\"schemaVersion\":2,\"nonce\":\"AA==\"}",              // version
    "{\"type\":\"challenge\",\"schemaVersion\":1,\"nonce\":\"AAEC\"}",              // nonce not 32 bytes
    "not json",
  };
  for(const char* frame:bad){
    CloudSession s(kPet,"0.2.0-mvp");s.connecting();s.opened(out,sizeof(out),0);
    TEST_ASSERT_EQUAL(int(CloudSession::Action::Fail),int(s.received(frame,std::strlen(frame),out,sizeof(out),len,0)));
    TEST_ASSERT_EQUAL(int(NetError::Protocol),int(s.lastError()));
  }
  CloudSession s=readySession(0);(void)s;
  CloudSession t(kPet,"0.2.0-mvp");t.connecting();t.opened(out,sizeof(out),0);
  const char challenge[]="{\"type\":\"challenge\",\"schemaVersion\":1,\"nonce\":\"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\"}";
  t.received(challenge,sizeof(challenge)-1,out,sizeof(out),len,0);
  const char tooShort[]="{\"type\":\"ready\",\"schemaVersion\":1,\"heartbeatS\":5}";
  TEST_ASSERT_EQUAL(int(CloudSession::Action::Fail),int(t.received(tooShort,sizeof(tooShort)-1,out,sizeof(out),len,0)));
}
void close_codes_and_backoff(){
  TEST_ASSERT_EQUAL(int(NetError::Unauthorized),int(CloudSession::errorFor(1008,"unauthorized")));
  TEST_ASSERT_EQUAL(int(NetError::NotBound),int(CloudSession::errorFor(1008,"not_bound")));
  TEST_ASSERT_EQUAL(int(NetError::ConsentRequired),int(CloudSession::errorFor(1008,"consent_required")));
  TEST_ASSERT_EQUAL(int(NetError::RateLimited),int(CloudSession::errorFor(1008,"rate_limited")));
  TEST_ASSERT_EQUAL(int(NetError::Replaced),int(CloudSession::errorFor(4000,"")));
  TEST_ASSERT_EQUAL(int(NetError::Protocol),int(CloudSession::errorFor(1009,"frame_too_large")));
  TEST_ASSERT_EQUAL(int(NetError::ServerUnreachable),int(CloudSession::errorFor(1001,"going_away")));
  CloudSession s(kPet,"x");
  // Exponential 2 s → 5 min; random 0 gives the lower jitter edge (75 %).
  const uint32_t expected[]={1500,3000,6000,12000,24000,48000,96000,192000,225000,225000};
  for(uint32_t e:expected)TEST_ASSERT_EQUAL_UINT32(e,s.ended(NetError::ServerUnreachable,0));
  TEST_ASSERT_EQUAL(int(CloudState::Backoff),int(s.state()));
  TEST_ASSERT_EQUAL_UINT32(1800000,s.ended(NetError::NotBound,0));
  TEST_ASSERT_EQUAL(int(CloudState::Rejected),int(s.state()));
  TEST_ASSERT_EQUAL_UINT32(225000,s.ended(NetError::Replaced,0));
  TEST_ASSERT_EQUAL_UINT32(375000,s.ended(NetError::RateLimited,150000));  // upper edge 125 %
  // A ready session resets the backoff.
  CloudSession r=readySession(0);
  TEST_ASSERT_EQUAL_UINT32(1500,r.ended(NetError::ServerUnreachable,0));
  TEST_ASSERT_FALSE(CloudSession("short","x").configured());
}
// Same inputs as backend tests/modules/identity/test_device_proof.py (session golden).
void session_proof_golden_and_never_over_ble(){
  static const uint8_t pet[16]={0xd9,0x45,0xc7,0x70,0x26,0xfd,0x4f,0x4a,0x8a,0x89,0x9b,0xbf,0xf4,0xae,0xed,0x86};
  uint8_t nonce[32];for(unsigned i=0;i<32;++i)nonce[i]=uint8_t(i);
  uint8_t m[DeviceProof::kMessageSize],d[32];char hex[65];
  DeviceProof::message(nonce,pet,ProofPurpose::Session,38,reinterpret_cast<const uint8_t*>("payload"),7,m);
  Sha256::hash(m,sizeof(m),d);for(int i=0;i<32;++i)std::snprintf(hex+2*i,3,"%02x",d[i]);
  TEST_ASSERT_EQUAL_STRING("448f0b9047eebce3a1cffe8f87c9f49f06ac73ff61aedd5855645906cc4b1aa4",hex);
  // A BLE central must not obtain a session proof: it could connect as the board.
  ProofService service;uint8_t request[DeviceProof::kRequestSize]={1,40,3,0,1,0,0,0};
  TEST_ASSERT_TRUE(service.accept(request,sizeof(request)));
  TEST_ASSERT_EQUAL(int(ProofStatus::UnsupportedPurpose),int(service.status()));
}
int main(){UNITY_BEGIN();RUN_TEST(base64_vectors);RUN_TEST(json_flat_lookups);RUN_TEST(handshake_and_heartbeat);
RUN_TEST(protocol_violations_fail);RUN_TEST(close_codes_and_backoff);
RUN_TEST(session_proof_golden_and_never_over_ble);return UNITY_END();}
