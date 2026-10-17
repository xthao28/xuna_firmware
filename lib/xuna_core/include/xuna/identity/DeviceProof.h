#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <xuna/base/Sha256.h>
#include <xuna/identity/IdentityRecord.h>
namespace xuna {
// Identity V2 device proof (contract draft §4; backend identity/domain/
// device_proof.py). The device signs SHA-256 of
//   "xuna.proof.v1" 0x00 | nonce[32] | petId[16] | purpose u8 | generation u32 LE
//   | SHA-256(payload)[32]
// with its ECDSA P-256 key. For Bind the payload is the device's own
// uncompressed public key, so a proof cannot be replayed for another key.
// Projection proofs are refused until the device can hash its own export.
enum class ProofPurpose : uint8_t { Bind=1,Projection=2 };
enum class ProofStatus : uint8_t {
  Idle=0,Pending=1,Ok=2,InvalidRequest=3,UnsupportedPurpose=4,NotReady=5,Failed=6
};
// Firmware adapter (mbedTLS). Keeps the private key; never exposes it.
class IDeviceSigner {
 public:
  virtual ~IDeviceSigner() {}
  virtual bool ready() const=0;
  virtual const uint8_t* publicKey() const=0;  // 65-byte uncompressed SEC1
  virtual bool sign(const uint8_t digest[32],uint8_t signature[64])=0;  // r||s
};
struct DeviceProof {
  static constexpr size_t kMessageSize=99,kNonceSize=32,kPublicKeySize=65,kSignatureSize=64;
  static constexpr size_t kRequestSize=40,kResponseSize=141;
  static void message(const uint8_t nonce[kNonceSize],const uint8_t petId[16],ProofPurpose purpose,
                      uint32_t generation,const uint8_t* payload,size_t payloadSize,
                      uint8_t out[kMessageSize]){
    static const char kDomain[]="xuna.proof.v1";  // + NUL = 14 bytes
    std::memcpy(out,kDomain,14);
    std::memcpy(out+14,nonce,kNonceSize);
    std::memcpy(out+46,petId,16);
    out[62]=static_cast<uint8_t>(purpose);
    IdentityRecord::put32(out+63,generation);
    Sha256::hash(payload,payloadSize,out+67);
  }
};
// One proof at a time. BLE writes `accept` a 40-byte request:
//   0 ver=1, 1 size=40, 2 purpose, 3 reserved=0, 4..7 requestId u32 LE (≠0), 8..39 nonce.
// `poll` (main loop) signs a pending request. `encode` gives the 141-byte READ value:
//   0 ver=1, 1 size=141, 2 purpose, 3 status, 4..7 requestId, 8..11 generation,
//   12..76 publicKey, 77..140 signature (zero unless status Ok).
class ProofService {
  uint8_t nonce_[DeviceProof::kNonceSize]={};
  uint8_t signature_[DeviceProof::kSignatureSize]={};
  uint8_t purpose_=0;
  ProofStatus status_=ProofStatus::Idle;
  uint32_t requestId_=0,generation_=0;
  bool keyShown_=false;
 public:
  // Returns false (and keeps the current proof) while one is pending.
  bool accept(const uint8_t* bytes,size_t size){
    if(status_==ProofStatus::Pending)return false;
    std::memset(signature_,0,sizeof(signature_));keyShown_=false;generation_=0;
    if(size!=DeviceProof::kRequestSize||bytes[0]!=1||bytes[1]!=DeviceProof::kRequestSize||bytes[3]!=0){
      purpose_=size>2?bytes[2]:0;requestId_=size>=8?IdentityRecord::get32(bytes+4):0;
      status_=ProofStatus::InvalidRequest;return true;
    }
    purpose_=bytes[2];requestId_=IdentityRecord::get32(bytes+4);
    std::memcpy(nonce_,bytes+8,sizeof(nonce_));
    if(!requestId_)status_=ProofStatus::InvalidRequest;
    else if(purpose_!=static_cast<uint8_t>(ProofPurpose::Bind))status_=ProofStatus::UnsupportedPurpose;
    else status_=ProofStatus::Pending;
    return true;
  }
  // identity: Ready record or null. Returns true when the READ value changed.
  bool poll(const IdentityRecord* identity,uint32_t generation,IDeviceSigner& signer){
    if(status_!=ProofStatus::Pending)return false;
    if(!identity||!identity->valid()||!signer.ready()){status_=ProofStatus::NotReady;return true;}
    uint8_t message[DeviceProof::kMessageSize],digest[Sha256::kSize];
    DeviceProof::message(nonce_,identity->petId,ProofPurpose::Bind,generation,
                         signer.publicKey(),DeviceProof::kPublicKeySize,message);
    Sha256::hash(message,sizeof(message),digest);
    generation_=generation;keyShown_=true;
    status_=signer.sign(digest,signature_)?ProofStatus::Ok:ProofStatus::Failed;
    if(status_!=ProofStatus::Ok)std::memset(signature_,0,sizeof(signature_));
    return true;
  }
  void reset(){*this=ProofService();}
  ProofStatus status() const { return status_; }
  void encode(const IDeviceSigner& signer,uint8_t out[DeviceProof::kResponseSize]) const {
    std::memset(out,0,DeviceProof::kResponseSize);
    out[0]=1;out[1]=uint8_t(DeviceProof::kResponseSize);out[2]=purpose_;out[3]=static_cast<uint8_t>(status_);
    IdentityRecord::put32(out+4,requestId_);IdentityRecord::put32(out+8,generation_);
    if(keyShown_&&signer.ready())std::memcpy(out+12,signer.publicKey(),DeviceProof::kPublicKeySize);
    if(status_==ProofStatus::Ok)std::memcpy(out+77,signature_,DeviceProof::kSignatureSize);
  }
};
}  // namespace xuna
