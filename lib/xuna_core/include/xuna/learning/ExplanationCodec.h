#pragma once
#include <cstddef>
#include <cstdint>
#include <xuna/learning/LearnerV3Explanation.h>
namespace xuna {
// S26 v1-additive 20-byte projection for the shown action only.
// 0 ver, 1 size, 2 interaction, 3 dayPart, 4 action, 5 reason,
// 6 flags(bit0 fine), 7 rated(saturated), then six little-endian Q8.8/u16:
// p, nEff, positive, negative, statedPositive, statedNegative.
class ExplanationCodec {
 static uint16_t q8(uint32_t q16){const uint32_t v=(q16+128U)>>8;return v>65535U?65535U:uint16_t(v);}
 static void put16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
 public:
 static constexpr size_t kSize=20;
 static bool encode(const V3Explanation& e,uint8_t out[kSize]){
  if(!e.valid||e.shownAction>=e.actionCount||e.actionCount>LearnerV3Config::kMaxActions)return false;
  const V3ExplanationCell& c=e.cells[e.shownAction];
  for(size_t i=0;i<kSize;++i)out[i]=0;
  out[0]=1;out[1]=kSize;out[2]=e.interaction;out[3]=e.dayPart;
  out[4]=e.shownAction;out[5]=uint8_t(e.reason);out[6]=c.fine?1:0;
  out[7]=c.rated>255?255:uint8_t(c.rated);
  put16(out+8,uint16_t((uint32_t(c.pQ16)*256U+32767U)/65535U));
  put16(out+10,q8(c.nEffQ16));put16(out+12,q8(c.positiveQ16));
  put16(out+14,q8(c.negativeQ16));put16(out+16,q8(c.statedPositiveQ16));
  put16(out+18,q8(c.statedNegativeQ16));return true;
 }
};
}  // namespace xuna
