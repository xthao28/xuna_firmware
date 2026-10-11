#pragma once
#include <cstdint>
#include <cstddef>
#include "LifeEngine.h"
namespace xuna {
struct BleCommand { uint32_t id=0, target=0; uint8_t opcode=0,arg=0,source=0,error=0; };
inline BleCommand parseCommand(const uint8_t* b,size_t n) {
  BleCommand c; if(n>=6)c.id=LifeEngine::get32(b+2);
  if(n!=8&&n!=12){c.error=2;return c;}
  if(b[0]!=1){c.error=1;return c;}
  c.opcode=b[1];c.arg=b[6];c.source=b[7];
  if(!c.id){c.error=4;return c;}
  if(c.opcode<1||c.opcode>4){c.error=3;return c;}
  if((c.opcode==3&&n!=12)||(c.opcode!=3&&n!=8)){c.error=2;return c;}
  if(c.opcode==3)c.target=LifeEngine::get32(b+8);
  if((c.opcode==1&&(c.arg>3||c.source))||(c.opcode==2&&(c.arg>1||c.source))||(c.opcode==3&&(c.arg>1||c.source<1||c.source>2||!c.target))||(c.opcode==4&&(c.arg||c.source)))c.error=4;
  return c;
}
class CommandCache {
  struct Entry { bool valid=false; uint8_t bytes[12]={};size_t size=0;uint8_t result=0; } entries_[16];
  size_t next_=0;
 public:
  void clear(){for(auto& e:entries_)e.valid=false;next_=0;}
  int find(const uint8_t* b,size_t n) const {
    if(n<6)return -1;
    for(const auto& e:entries_)if(e.valid&&LifeEngine::get32(e.bytes+2)==LifeEngine::get32(b+2)){
      if(e.size!=n)return 6;
      for(size_t i=0;i<n;++i)if(e.bytes[i]!=b[i])return 6;
      return e.result;
    }return -1;
  }
  void remember(const uint8_t* b,size_t n,uint8_t result){
    if(n>12||n<6)return;
    auto& e=entries_[next_];e.valid=true;e.size=n;e.result=result;
    for(size_t i=0;i<n;++i)e.bytes[i]=b[i];next_=(next_+1)%16;
  }
};
}
