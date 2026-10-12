#pragma once
#include <cstdint>
#include <cstddef>
namespace xuna {
// Explicit feedback trains a two-action Beta/Bernoulli preference estimate.
// Two contexts: quiet (0), play (1). No wall clock is invented.
class LifeEngine {
 public:
  static constexpr size_t kSnapshotSize = 96;
  struct Episode { uint8_t context=0, action=0, approved=0, source=0; };
  uint16_t positive[2][2] = {}, negative[2][2] = {};
  Episode episodes[8];
  uint8_t episodeCount=0, episodeNext=0;
  uint32_t activeSeconds=0, eventCount=0, generation=0;
  uint8_t choose(uint8_t context) const {
    if (context > 1) return 0;
    const uint32_t a=positive[context][0]+1, b=positive[context][1]+1;
    const uint32_t ta=positive[context][0]+negative[context][0]+2;
    const uint32_t tb=positive[context][1]+negative[context][1]+2;
    return b*ta > a*tb ? 1 : 0;
  }
  void feedback(uint8_t context, uint8_t action, bool approved, uint8_t source) {
    if (context>1 || action>1 || source<1 || source>2) return;
    auto& count=approved ? positive[context][action] : negative[context][action];
    // Bound counters and retain relative evidence when nearing saturation.
    if (count>=1000) {
      positive[context][action]=(positive[context][action]+1)/2;
      negative[context][action]=(negative[context][action]+1)/2;
    }
    ++count; ++eventCount;
    auto& e=episodes[episodeNext]; e.context=context; e.action=action;
    e.approved=approved; e.source=source;
    episodeNext=(episodeNext+1)%8; if (episodeCount<8) ++episodeCount;
  }
  bool recover(const uint8_t* firstBytes,size_t firstSize,const uint8_t* secondBytes,size_t secondSize) {
    LifeEngine first,second;
    const bool a=first.decode(firstBytes,firstSize),b=second.decode(secondBytes,secondSize);
    if(!a&&!b)return false;
    *this=b&&(!a||int32_t(second.generation-first.generation)>0)?second:first;return true;
  }
  static void put32(uint8_t* b,uint32_t v) { for(int i=0;i<4;++i)b[i]=v>>(8*i); }
  static uint32_t get32(const uint8_t* b) { return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24); }
  static uint32_t checksum(const uint8_t* b,size_t n) {
    uint32_t c=2166136261U; for(size_t i=0;i<n;++i)c=(c^b[i])*16777619U; return c;
  }
  void encode(uint8_t* b) const {
    for(size_t i=0;i<kSnapshotSize;++i)b[i]=0;
    b[0]='X';b[1]='U';b[2]=1;b[3]=kSnapshotSize;
    put32(b+4,generation);put32(b+8,activeSeconds);put32(b+12,eventCount);
    size_t p=16;
    for(int c=0;c<2;++c)for(int a=0;a<2;++a){
      b[p++]=positive[c][a]&255;b[p++]=positive[c][a]>>8;
      b[p++]=negative[c][a]&255;b[p++]=negative[c][a]>>8;
    }
    b[32]=episodeCount;b[33]=episodeNext;
    for(int i=0;i<8;++i){b[34+i*4]=episodes[i].context;b[35+i*4]=episodes[i].action;b[36+i*4]=episodes[i].approved;b[37+i*4]=episodes[i].source;}
    put32(b+92,checksum(b,92));
  }
  bool decode(const uint8_t* b,size_t size) {
    if(size!=kSnapshotSize||b[0]!='X'||b[1]!='U'||b[2]!=1||b[3]!=kSnapshotSize||get32(b+92)!=checksum(b,92)||b[32]>8||b[33]>7)return false;
    LifeEngine candidate; candidate.generation=get32(b+4);candidate.activeSeconds=get32(b+8);candidate.eventCount=get32(b+12);
    size_t p=16;
    for(int c=0;c<2;++c)for(int a=0;a<2;++a){candidate.positive[c][a]=b[p]|(uint16_t(b[p+1])<<8);p+=2;candidate.negative[c][a]=b[p]|(uint16_t(b[p+1])<<8);p+=2;if(candidate.positive[c][a]>1001||candidate.negative[c][a]>1001)return false;}
    candidate.episodeCount=b[32];candidate.episodeNext=b[33];
    for(int i=0;i<8;++i){auto& e=candidate.episodes[i];e.context=b[34+i*4];e.action=b[35+i*4];e.approved=b[36+i*4];e.source=b[37+i*4];if(e.context>1||e.action>1||e.approved>1||e.source>2)return false;}
    for(uint8_t i=0;i<candidate.episodeCount;++i){
      const uint8_t oldest=candidate.episodeCount==8?candidate.episodeNext:0;
      if(candidate.episodes[(oldest+i)%8].source<1)return false;
    }
    *this=candidate;return true;
  }
};
}
