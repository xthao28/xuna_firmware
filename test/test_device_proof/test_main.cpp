#include <unity.h>
#include <cstdio>
#include <cstring>
#include <xuna/identity/DeviceProof.h>
using namespace xuna;
static void hex(const uint8_t* b,size_t n,char* out){for(size_t i=0;i<n;++i)std::snprintf(out+2*i,3,"%02x",b[i]);}
static void expectHash(const char* expected,const uint8_t* data,size_t size){
  uint8_t d[32];char s[65];Sha256::hash(data,size,d);hex(d,32,s);TEST_ASSERT_EQUAL_STRING(expected,s);
}
void sha256_vectors(){
  expectHash("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",nullptr,0);
  expectHash("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",(const uint8_t*)"abc",3);
  const char* two="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  expectHash("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",(const uint8_t*)two,std::strlen(two));
  Sha256 s;uint8_t a[1000];std::memset(a,'a',sizeof(a));
  for(int i=0;i<1000;++i)s.update(a,sizeof(a));
  uint8_t d[32];char t[65];s.finish(d);hex(d,32,t);
  TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",t);
}
static const uint8_t kPet[16]={0xd9,0x45,0xc7,0x70,0x26,0xfd,0x4f,0x4a,0x8a,0x89,0x9b,0xbf,0xf4,0xae,0xed,0x86};
// Same inputs as backend tests/modules/identity/test_device_proof.py.
void golden_message_matches_backend(){
  uint8_t nonce[32];for(unsigned i=0;i<32;++i)nonce[i]=uint8_t(i);
  uint8_t m[DeviceProof::kMessageSize];
  DeviceProof::message(nonce,kPet,ProofPurpose::Bind,38,(const uint8_t*)"payload",7,m);
  TEST_ASSERT_EQUAL_MEMORY("xuna.proof.v1\0",m,14);
  TEST_ASSERT_EQUAL_MEMORY(nonce,m+14,32);
  TEST_ASSERT_EQUAL_MEMORY(kPet,m+46,16);
  const uint8_t tail[5]={1,38,0,0,0};TEST_ASSERT_EQUAL_MEMORY(tail,m+62,5);
  expectHash("2ec55d344e9c195c9c3dc17123cb48f3157b76e6d18da9ddea18dd2a630ccb4c",m,sizeof(m));
}
class FakeSigner: public IDeviceSigner {
 public:
  uint8_t key[65];bool isReady=true,ok=true;uint8_t lastDigest[32]={};int calls=0;
  FakeSigner(){key[0]=4;for(unsigned i=1;i<65;++i)key[i]=uint8_t(i);}
  bool ready() const override {return isReady;}
  const uint8_t* publicKey() const override {return key;}
  bool sign(const uint8_t digest[32],uint8_t sig[64]) override {
    ++calls;std::memcpy(lastDigest,digest,32);for(unsigned i=0;i<64;++i)sig[i]=uint8_t(0xa0+i);return ok;
  }
};
static IdentityRecord identity(){
  IdentityRecord r;r.hatchBootCounter=1;r.origin=IdentityOrigin::MigratedFromV1;std::memcpy(r.petId,kPet,16);return r;
}
static void request(uint8_t* b,uint8_t purpose,uint32_t id){
  std::memset(b,0,40);b[0]=1;b[1]=40;b[2]=purpose;IdentityRecord::put32(b+4,id);for(unsigned i=0;i<32;++i)b[8+i]=uint8_t(i);
}
void bind_proof_signs_own_key(){
  ProofService service;FakeSigner signer;const IdentityRecord id=identity();
  uint8_t out[DeviceProof::kResponseSize];service.encode(signer,out);
  TEST_ASSERT_EQUAL_UINT8(141,out[1]);TEST_ASSERT_EQUAL_UINT8(0,out[3]);
  uint8_t req[40];request(req,1,7);
  TEST_ASSERT_TRUE(service.accept(req,40));TEST_ASSERT_EQUAL(ProofStatus::Pending,service.status());
  TEST_ASSERT_FALSE(service.accept(req,40));  // one in flight
  TEST_ASSERT_TRUE(service.poll(&id,38,signer));TEST_ASSERT_FALSE(service.poll(&id,38,signer));
  TEST_ASSERT_EQUAL(1,signer.calls);
  uint8_t m[99],digest[32];DeviceProof::message(req+8,kPet,ProofPurpose::Bind,38,signer.key,65,m);Sha256::hash(m,99,digest);
  TEST_ASSERT_EQUAL_MEMORY(digest,signer.lastDigest,32);
  service.encode(signer,out);
  TEST_ASSERT_EQUAL_UINT8(1,out[2]);TEST_ASSERT_EQUAL_UINT8(2,out[3]);
  TEST_ASSERT_EQUAL_UINT32(7,IdentityRecord::get32(out+4));TEST_ASSERT_EQUAL_UINT32(38,IdentityRecord::get32(out+8));
  TEST_ASSERT_EQUAL_MEMORY(signer.key,out+12,65);TEST_ASSERT_EQUAL_UINT8(0xa0,out[77]);TEST_ASSERT_EQUAL_UINT8(0xdf,out[140]);
  service.reset();service.encode(signer,out);
  for(size_t i=2;i<sizeof(out);++i)TEST_ASSERT_EQUAL_UINT8(0,out[i]);
}
void rejects_bad_requests_without_signing(){
  ProofService service;FakeSigner signer;const IdentityRecord id=identity();uint8_t req[40],out[141];
  request(req,2,9);service.accept(req,40);TEST_ASSERT_EQUAL(ProofStatus::UnsupportedPurpose,service.status());
  TEST_ASSERT_FALSE(service.poll(&id,1,signer));
  request(req,1,0);service.accept(req,40);TEST_ASSERT_EQUAL(ProofStatus::InvalidRequest,service.status());
  request(req,1,5);service.accept(req,39);TEST_ASSERT_EQUAL(ProofStatus::InvalidRequest,service.status());
  request(req,1,5);req[3]=1;service.accept(req,40);TEST_ASSERT_EQUAL(ProofStatus::InvalidRequest,service.status());
  request(req,1,5);req[0]=2;service.accept(req,40);TEST_ASSERT_EQUAL(ProofStatus::InvalidRequest,service.status());
  service.encode(signer,out);for(size_t i=12;i<141;++i)TEST_ASSERT_EQUAL_UINT8(0,out[i]);
  TEST_ASSERT_EQUAL(0,signer.calls);
}
void not_ready_and_failure_expose_no_signature(){
  ProofService service;FakeSigner signer;uint8_t req[40],out[141];
  request(req,1,3);service.accept(req,40);service.poll(nullptr,1,signer);
  TEST_ASSERT_EQUAL(ProofStatus::NotReady,service.status());
  signer.isReady=false;const IdentityRecord id=identity();
  request(req,1,4);service.accept(req,40);service.poll(&id,1,signer);TEST_ASSERT_EQUAL(ProofStatus::NotReady,service.status());
  signer.isReady=true;signer.ok=false;request(req,1,5);service.accept(req,40);service.poll(&id,1,signer);
  TEST_ASSERT_EQUAL(ProofStatus::Failed,service.status());service.encode(signer,out);
  for(size_t i=77;i<141;++i)TEST_ASSERT_EQUAL_UINT8(0,out[i]);
}
void setUp(){} void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(sha256_vectors);RUN_TEST(golden_message_matches_backend);RUN_TEST(bind_proof_signs_own_key);
RUN_TEST(rejects_bad_requests_without_signing);RUN_TEST(not_ready_and_failure_expose_no_signature);return UNITY_END();}
