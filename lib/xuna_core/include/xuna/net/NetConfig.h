#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace xuna {
// Wi-Fi setup over BLE (DC-2, characteristic 7a6e0011, write only). The app
// splits one message into frames [version 1, id, index, count, payload…] like
// the speech subtitles; the message is
//   op 1 set:    ssidLen u8, ssid, passLen u8, pass, hostLen u8, host, port u16 LE
//   op 2 forget: nothing else
// The password is never echoed, logged or projected; buffers are wiped.
struct NetConfig {
  char ssid[33]={};
  char pass[65]={};
  char host[65]={};
  uint16_t port=0;
  bool valid() const {return ssid[0]&&host[0]&&port;}
  void wipe(){std::memset(this,0,sizeof(*this));}
};
enum class NetOp : uint8_t { None=0, Set=1, Forget=2 };
class NetConfigCodec {
 public:
  static constexpr uint8_t kVersion=1;
  static constexpr size_t kHeader=4;
  static constexpr size_t kMaxMessage=1+1+32+1+64+1+64+2;
  static constexpr size_t kMaxFrame=244;  // ATT MTU 247 - 3
  static constexpr uint8_t kMaxChunks=16;
  enum class Result : uint8_t { Partial, Ready, Rejected };
  // One frame; Ready once a whole, valid message arrived (then op()/config()).
  Result feed(const uint8_t* b,size_t n) {
    if(!b||n<kHeader||n>kMaxFrame||b[0]!=kVersion)return reject();
    const uint8_t id=b[1],index=b[2],count=b[3];
    const size_t payload=n-kHeader;
    if(!count||count>kMaxChunks||index>=count)return reject();
    if(index==0){open_=true;id_=id;count_=count;next_=0;size_=0;}
    else if(!open_||id!=id_||count!=count_||index!=next_)return reject();
    if(size_+payload>kMaxMessage)return reject();
    std::memcpy(raw_+size_,b+kHeader,payload);size_+=payload;next_=uint8_t(index+1);
    if(next_<count_)return Result::Partial;
    open_=false;
    const bool ok=decode(raw_,size_,op_,config_);
    std::memset(raw_,0,sizeof(raw_));size_=0;
    if(!ok){op_=NetOp::None;config_.wipe();return Result::Rejected;}
    return Result::Ready;
  }
  NetOp op() const {return op_;}
  const NetConfig& config() const {return config_;}
  void clear(){op_=NetOp::None;config_.wipe();}
  static bool decode(const uint8_t* m,size_t n,NetOp& op,NetConfig& out) {
    if(!m||!n)return false;
    if(m[0]==uint8_t(NetOp::Forget)){if(n!=1)return false;op=NetOp::Forget;out.wipe();return true;}
    if(m[0]!=uint8_t(NetOp::Set))return false;
    NetConfig c;size_t at=1;
    if(!field(m,n,at,c.ssid,1,32)||!field(m,n,at,c.pass,0,64)||!field(m,n,at,c.host,1,64)||at+2!=n){c.wipe();return false;}
    c.port=uint16_t(m[at]|(m[at+1]<<8));
    const size_t passLen=std::strlen(c.pass);
    if(!c.port||(passLen&&passLen<8)||!hostOk(c.host)||!printable(c.ssid)||!printable(c.pass)){c.wipe();return false;}
    out=c;c.wipe();op=NetOp::Set;return true;
  }
  // Message builder (tests; the app has its own Dart codec).
  static size_t encodeSet(const NetConfig& c,uint8_t* out,size_t cap) {
    const size_t s=std::strlen(c.ssid),p=std::strlen(c.pass),h=std::strlen(c.host),n=1+1+s+1+p+1+h+2;
    if(n>cap)return 0;
    size_t at=0;out[at++]=uint8_t(NetOp::Set);
    out[at++]=uint8_t(s);std::memcpy(out+at,c.ssid,s);at+=s;
    out[at++]=uint8_t(p);std::memcpy(out+at,c.pass,p);at+=p;
    out[at++]=uint8_t(h);std::memcpy(out+at,c.host,h);at+=h;
    out[at++]=uint8_t(c.port);out[at++]=uint8_t(c.port>>8);return at;
  }
 private:
  static bool field(const uint8_t* m,size_t n,size_t& at,char* dst,size_t minLen,size_t maxLen) {
    if(at>=n)return false;
    const size_t len=m[at++];
    if(len<minLen||len>maxLen||at+len>n)return false;
    for(size_t i=0;i<len;++i){if(!m[at+i])return false;dst[i]=char(m[at+i]);}
    dst[len]=0;at+=len;return true;
  }
  // SSIDs and passphrases may hold UTF-8, but no control bytes.
  static bool printable(const char* s){for(;*s;++s)if(uint8_t(*s)<0x20||uint8_t(*s)==0x7f)return false;return true;}
  static bool hostOk(const char* s) {
    for(;*s;++s){const char c=*s;if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'))return false;}
    return true;
  }
  Result reject(){open_=false;std::memset(raw_,0,sizeof(raw_));size_=0;return Result::Rejected;}
  uint8_t raw_[kMaxMessage]={};size_t size_=0;
  bool open_=false;uint8_t id_=0,count_=0,next_=0;
  NetOp op_=NetOp::None;NetConfig config_;
};
// Network projection (DC-2, characteristic 7a6e0012, read + notify):
// [version 1, wifi, cloud, rssi i8, lastError, lastBackupGeneration u32 LE].
// No SSID, host or password.
enum class WifiState : uint8_t { NotConfigured=0, Joining=1, Connected=2, Failed=3 };
enum class CloudState : uint8_t { Off=0, Connecting=1, Proving=2, Ready=3, Backoff=4, Rejected=5 };
enum class NetError : uint8_t {
  None=0, WifiNotFound=1, WifiAuth=2, WifiOther=3, ServerUnreachable=4, Tls=5,
  Unauthorized=6, NotBound=7, ConsentRequired=8, RateLimited=9, Protocol=10, Replaced=11
};
struct NetStatus {
  WifiState wifi=WifiState::NotConfigured;
  CloudState cloud=CloudState::Off;
  int8_t rssi=0;
  NetError lastError=NetError::None;
  uint32_t lastBackupGeneration=0;
  static constexpr size_t kSize=9;
  void encode(uint8_t out[kSize]) const {
    out[0]=1;out[1]=uint8_t(wifi);out[2]=uint8_t(cloud);out[3]=uint8_t(rssi);out[4]=uint8_t(lastError);
    for(int i=0;i<4;++i)out[5+i]=uint8_t(lastBackupGeneration>>(8*i));
  }
};
}
