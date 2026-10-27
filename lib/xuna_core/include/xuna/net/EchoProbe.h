#pragma once
#include <cstddef>
#include <cstdint>
namespace xuna {
// E14 echo traffic (DC-1): a fixed 1 KB synthetic text frame every period,
// one in flight, checked byte for byte when the server echoes it. Content is
// generated, never pet data. Pure: the network task supplies time and bytes.
class EchoProbe {
 public:
  static constexpr size_t kSize=1024;
  static constexpr size_t kHeader=24;  // "xuna.e14 seq=0000000001 "
  explicit EchoProbe(uint32_t periodMs=5000):periodMs_(periodMs){}
  static void encode(uint32_t seq,char out[kSize]) {
    static const char kPrefix[]="xuna.e14 seq=";
    size_t i=0;
    for(;kPrefix[i];++i)out[i]=kPrefix[i];
    uint32_t v=seq;
    for(size_t d=0;d<10;++d){out[i+9-d]=char('0'+v%10);v/=10;}
    i+=10;out[i++]=' ';
    for(;i<kSize;++i)out[i]=char('a'+(seq+i)%26);
  }
  // True only for a complete, intact frame; `seq` is then its number.
  static bool decode(const char* in,size_t size,uint32_t& seq) {
    static const char kPrefix[]="xuna.e14 seq=";
    if(!in||size!=kSize)return false;
    for(size_t i=0;kPrefix[i];++i)if(in[i]!=kPrefix[i])return false;
    uint32_t v=0;
    for(size_t d=0;d<10;++d){
      const char c=in[13+d];
      if(c<'0'||c>'9')return false;
      if(v>429496729U||(v==429496729U&&c>'5'))return false;  // > u32
      v=v*10+uint32_t(c-'0');
    }
    if(in[kHeader-1]!=' ')return false;
    for(size_t i=kHeader;i<kSize;++i)if(in[i]!=char('a'+(v+i)%26))return false;
    seq=v;return true;
  }
  // A new frame is due (first one immediately after connect).
  bool due(uint32_t nowMs) const {return !started_||uint32_t(nowMs-sentAt_)>=periodMs_;}
  // Starts the next frame. An unanswered previous frame counts as lost.
  uint32_t begin(uint32_t nowMs,char out[kSize]) {
    if(inFlight_){++lost_;++lostInRow_;}
    ++seq_;inFlight_=true;started_=true;sentAt_=nowMs;++sent_;
    encode(seq_,out);return seq_;
  }
  void received(const char* data,size_t size,uint32_t nowMs) {
    uint32_t seq=0;
    if(!decode(data,size,seq)||!inFlight_||seq!=seq_){++bad_;return;}
    inFlight_=false;lostInRow_=0;++ok_;
    const uint32_t rtt=nowMs-sentAt_;rttSumMs_+=rtt;if(rtt>rttMaxMs_)rttMaxMs_=rtt;
  }
  // Connection gone: an in-flight frame is dropped, not lost (no server to answer).
  void disconnected(){if(inFlight_)++dropped_;inFlight_=false;started_=false;lostInRow_=0;}
  uint32_t sent() const {return sent_;}
  uint32_t ok() const {return ok_;}
  uint32_t bad() const {return bad_;}
  uint32_t lost() const {return lost_;}
  uint32_t dropped() const {return dropped_;}
  uint32_t lostInRow() const {return lostInRow_;}
  uint32_t rttMaxMs() const {return rttMaxMs_;}
  uint32_t rttMeanMs() const {return ok_?uint32_t(rttSumMs_/ok_):0;}
 private:
  uint32_t periodMs_,seq_=0,sentAt_=0;
  bool inFlight_=false,started_=false;
  uint32_t sent_=0,ok_=0,bad_=0,lost_=0,dropped_=0,lostInRow_=0,rttMaxMs_=0;
  uint64_t rttSumMs_=0;
};
}
