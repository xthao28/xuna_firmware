#pragma once
#include <cstdint>
#include <cstddef>
#include <xuna/LifeEngine.h>
namespace xuna {
struct BleCommand {
  uint32_t id=0, target=0, utcSeconds=0;
  int16_t tzOffsetMin=0;
  uint8_t opcode=0,arg=0,source=0,error=0,contextKey=0,action=0;
};
// Opcode 6 StatedPreference (plan §11.6, 12 bytes: arg = polarity, source 2 =
// confirmed text, contextKey @8, actionId @9, reserved u16 @10 = 0) is parsed
// only when `statedPreference` is set (S26 enhanced production adapter).
// Baseline v1 adapters keep opcode 6 unknown (error 3). Source 3 (voice)
// stays invalid until D16. Context/action repertoire checks belong to the core.
// Opcode 1 args 4–7 (Track A variant preview, capability byte 4 bits 4–7) are
// valid only when `variantPreview` is set; v1 keeps 0–3.
// Opcode 8 ConversationSignal (PE-4c, 8 bytes, arg = tag 1–6, source 0,
// capability byte 3 bit 2) is parsed only when `conversation` is set.
inline BleCommand parseCommand(const uint8_t* b,size_t n,
                               bool statedPreference=false,
                               bool timeSync=false,bool hatch=false,
                               bool variantPreview=false,
                               bool conversation=false) {
  BleCommand c; if(n>=6)c.id=LifeEngine::get32(b+2);
  if(n!=8&&n!=12&&(!timeSync||n!=16)){c.error=2;return c;}
  if(b[0]!=1){c.error=1;return c;}
  c.opcode=b[1];c.arg=b[6];c.source=b[7];
  if(!c.id){c.error=4;return c;}
  const bool stated=statedPreference&&c.opcode==6;
  const bool sync=timeSync&&c.opcode==5;
  const bool hatchCommand=hatch&&c.opcode==7;
  const bool talk=conversation&&c.opcode==8;
  if((c.opcode<1||c.opcode>4)&&!stated&&!sync&&!hatchCommand&&!talk){c.error=3;return c;}
  const size_t expected=sync?16:(c.opcode==3||stated?12:8);
  if(n!=expected){c.error=2;return c;}
  if(c.opcode==3)c.target=LifeEngine::get32(b+8);
  if(stated){c.contextKey=b[8];c.action=b[9];}
  if(sync){
    c.source=b[6];c.arg=0;c.utcSeconds=LifeEngine::get32(b+8);
    c.tzOffsetMin=static_cast<int16_t>(uint16_t(b[12])|(uint16_t(b[13])<<8));
  }
  if((c.opcode==1&&(c.arg>(variantPreview?7:3)||c.source))||(c.opcode==2&&(c.arg>1||c.source))||(c.opcode==3&&(c.arg>1||c.source<1||c.source>2||!c.target))||(c.opcode==4&&(c.arg||c.source)))c.error=4;
  if(stated&&(c.arg>1||c.source!=2||b[10]||b[11]))c.error=4;
  if(sync&&(c.source!=1||b[7]||!c.utcSeconds||c.tzOffsetMin < -840||
            c.tzOffsetMin > 840||b[14]||b[15]))c.error=4;
  if(hatchCommand&&(c.arg||c.source))c.error=4;
  if(talk&&(c.arg<1||c.arg>6||c.source))c.error=4;
  return c;
}
class CommandCache {
  struct Entry { bool valid=false; uint8_t bytes[16]={};size_t size=0;uint8_t result=0; } entries_[16];
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
    if((n>12&&n!=16)||n<6)return;
    auto& e=entries_[next_];e.valid=true;e.size=n;e.result=result;
    for(size_t i=0;i<n;++i)e.bytes[i]=b[i];
    next_=(next_+1)%16;
  }
};
}
