#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <xuna/base/Base64.h>
#include <xuna/net/NetConfig.h>
namespace xuna {
// Flat JSON lookups for the server's fixed frames (contract "Device
// connection"): one object, no nesting needed, keys unique. Tolerates
// whitespace; a field that is missing or of the wrong type is "not found".
struct JsonFlat {
  // Pointer just after `"key":` (whitespace skipped), or nullptr.
  static const char* field(const char* json,size_t n,const char* key) {
    const size_t k=std::strlen(key);
    for(size_t i=0;i+k+2<n;++i){
      if(json[i]!='"'||std::strncmp(json+i+1,key,k)||json[i+1+k]!='"')continue;
      size_t j=i+2+k;
      while(j<n&&(json[j]==' '||json[j]=='\t'||json[j]=='\r'||json[j]=='\n'))++j;
      if(j>=n||json[j]!=':')continue;
      ++j;
      while(j<n&&(json[j]==' '||json[j]=='\t'||json[j]=='\r'||json[j]=='\n'))++j;
      return j<n?json+j:nullptr;
    }
    return nullptr;
  }
  // A string without escapes into `out` (NUL-terminated); false otherwise.
  static bool string(const char* json,size_t n,const char* key,char* out,size_t cap) {
    const char* v=field(json,n,key);
    if(!v||*v!='"')return false;
    const char* end=json+n;size_t o=0;
    for(++v;v<end&&*v!='"';++v){
      if(*v=='\\'||o+1>=cap)return false;
      out[o++]=*v;
    }
    if(v>=end)return false;
    out[o]=0;return true;
  }
  static bool uint(const char* json,size_t n,const char* key,uint32_t& out) {
    const char* v=field(json,n,key);
    if(!v||*v<'0'||*v>'9')return false;
    const char* end=json+n;uint64_t value=0;
    for(;v<end&&*v>='0'&&*v<='9';++v){value=value*10+uint32_t(*v-'0');if(value>0xFFFFFFFFull)return false;}
    out=uint32_t(value);return true;
  }
};
// Device side of `/v1/devices/connect` (DC-4). Pure: the network task feeds
// transport events and frames, and sends what it returns. It never touches
// life state; the only device data it carries are PetId, firmware version
// and the snapshot generation in the proof.
class CloudSession {
 public:
  static constexpr size_t kMaxFrame=512;
  static constexpr uint32_t kBackoffMinMs=2000,kBackoffMaxMs=300000;
  static constexpr uint32_t kLongWaitMs=1800000;  // not_bound / consent_required
  enum class Action : uint8_t { None, Sign, Send, Ready, Fail };
  explicit CloudSession(const char* petIdHex="",const char* firmware=""){configure(petIdHex,firmware);}
  void configure(const char* petIdHex,const char* firmware) {
    std::snprintf(petId_,sizeof(petId_),"%s",petIdHex);std::snprintf(firmware_,sizeof(firmware_),"%s",firmware);
  }
  bool configured() const {return std::strlen(petId_)==32;}
  CloudState state() const {return state_;}
  NetError lastError() const {return error_;}
  uint32_t heartbeatMs() const {return heartbeatS_*1000u;}
  const uint8_t* nonce() const {return nonce_;}

  void connecting(){state_=CloudState::Connecting;}
  // TLS + WebSocket up: the hello to send.
  size_t opened(char* out,size_t cap,uint32_t nowMs) {
    state_=CloudState::Proving;phase_=Phase::AwaitChallenge;lastPongMs_=nowMs;
    const int n=std::snprintf(out,cap,
        "{\"type\":\"hello\",\"schemaVersion\":1,\"petId\":\"%s\",\"firmware\":\"%s\",\"capabilities\":[]}",
        petId_,firmware_);
    return n>0&&size_t(n)<cap?size_t(n):0;
  }
  // One text frame from the server. Sign → call signed(); Send → `out` holds
  // the reply; Ready → session ready; Fail → close the transport.
  Action received(const char* json,size_t n,char* out,size_t cap,size_t& outLen,uint32_t nowMs) {
    outLen=0;
    char type[16];
    if(!JsonFlat::string(json,n,"type",type,sizeof(type)))return fail(NetError::Protocol);
    uint32_t version=0;
    if(!JsonFlat::uint(json,n,"schemaVersion",version)||version!=1)return fail(NetError::Protocol);
    if(phase_==Phase::AwaitChallenge){
      if(std::strcmp(type,"challenge"))return fail(NetError::Protocol);
      char nonce[48];
      if(!JsonFlat::string(json,n,"nonce",nonce,sizeof(nonce))||
         !Base64::decode(nonce,std::strlen(nonce),nonce_,sizeof(nonce_)))return fail(NetError::Protocol);
      phase_=Phase::AwaitSignature;return Action::Sign;
    }
    if(phase_==Phase::AwaitReady){
      if(std::strcmp(type,"ready"))return fail(NetError::Protocol);
      uint32_t heartbeat=0;
      if(!JsonFlat::uint(json,n,"heartbeatS",heartbeat)||heartbeat<10||heartbeat>600)return fail(NetError::Protocol);
      heartbeatS_=heartbeat;phase_=Phase::Ready;state_=CloudState::Ready;error_=NetError::None;
      attempts_=0;lastPingMs_=nowMs;lastPongMs_=nowMs;return Action::Ready;
    }
    if(phase_==Phase::Ready){
      if(!std::strcmp(type,"pong")){
        uint32_t seq=0;
        if(JsonFlat::uint(json,n,"seq",seq)&&seq==pingSeq_)lastPongMs_=nowMs;
      }
      return Action::None;  // unknown server messages are ignored, never acted on (H2)
    }
    (void)out;(void)cap;
    return fail(NetError::Protocol);  // nothing is expected while signing
  }
  // The proof once the device key signed nonce() (purpose 3, payload = own key).
  size_t signedProof(uint32_t generation,const uint8_t signature[64],char* out,size_t cap) {
    if(phase_!=Phase::AwaitSignature)return 0;
    char sig[Base64::encodedSize(64)+1];Base64::encode(signature,64,sig,sizeof(sig));
    const int n=std::snprintf(out,cap,"{\"type\":\"proof\",\"schemaVersion\":1,\"generation\":%lu,\"signature\":\"%s\"}",
                              static_cast<unsigned long>(generation),sig);
    if(n<=0||size_t(n)>=cap)return 0;
    phase_=Phase::AwaitReady;return size_t(n);
  }
  // Heartbeat while Ready: a ping when due; Fail after two missed pongs.
  Action tick(uint32_t nowMs,char* out,size_t cap,size_t& outLen) {
    outLen=0;
    if(phase_!=Phase::Ready)return Action::None;
    if(uint32_t(nowMs-lastPongMs_)>3*heartbeatMs())return fail(NetError::ServerUnreachable);
    if(uint32_t(nowMs-lastPingMs_)<heartbeatMs())return Action::None;
    ++pingSeq_;lastPingMs_=nowMs;
    const int n=std::snprintf(out,cap,"{\"type\":\"ping\",\"schemaVersion\":1,\"seq\":%lu}",
                              static_cast<unsigned long>(pingSeq_));
    if(n<=0||size_t(n)>=cap)return Action::None;
    outLen=size_t(n);return Action::Send;
  }
  // The server closed with a code and reason (contract close table).
  static NetError errorFor(uint16_t code,const char* reason) {
    if(!std::strcmp(reason,"unauthorized"))return NetError::Unauthorized;
    if(!std::strcmp(reason,"not_bound"))return NetError::NotBound;
    if(!std::strcmp(reason,"consent_required"))return NetError::ConsentRequired;
    if(!std::strcmp(reason,"rate_limited"))return NetError::RateLimited;
    if(!std::strcmp(reason,"replaced")||code==4000)return NetError::Replaced;
    if(code==1002||code==1003||code==1007||code==1009||!std::strcmp(reason,"unsupported_protocol")||
       !std::strcmp(reason,"frame_too_large"))return NetError::Protocol;
    return NetError::ServerUnreachable;  // idle_timeout, going_away, other
  }
  // Transport gone (close frame, TLS or socket error): Backoff, and how long
  // to wait before the next attempt. `random` adds ±25 % jitter.
  uint32_t ended(NetError error,uint32_t random) {
    error_=error;phase_=Phase::Idle;
    if(error==NetError::NotBound||error==NetError::ConsentRequired){state_=CloudState::Rejected;return kLongWaitMs;}
    state_=error==NetError::Unauthorized?CloudState::Rejected:CloudState::Backoff;
    if(error==NetError::Replaced||error==NetError::RateLimited||error==NetError::Protocol)return jitter(kBackoffMaxMs,random);
    uint32_t delay=kBackoffMinMs;
    for(uint32_t i=0;i<attempts_&&delay<kBackoffMaxMs;++i)delay*=2;
    if(delay>kBackoffMaxMs)delay=kBackoffMaxMs;
    if(attempts_<32)++attempts_;
    return jitter(delay,random);
  }
  void stop(){state_=CloudState::Off;phase_=Phase::Idle;attempts_=0;}
 private:
  enum class Phase : uint8_t { Idle, AwaitChallenge, AwaitSignature, AwaitReady, Ready };
  Action fail(NetError e){error_=e;return Action::Fail;}
  static uint32_t jitter(uint32_t ms,uint32_t random){return ms-ms/4+random%(ms/2+1);}
  char petId_[33]={},firmware_[33]={};
  uint8_t nonce_[32]={};
  CloudState state_=CloudState::Off;NetError error_=NetError::None;Phase phase_=Phase::Idle;
  uint32_t heartbeatS_=30,attempts_=0,pingSeq_=0,lastPingMs_=0,lastPongMs_=0;
};
}
