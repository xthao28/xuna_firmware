#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <xuna/base/Sha256.h>
namespace xuna {
// `xuna.export.v1` written by the board itself (DC-5) from the same BLE
// projection bytes the app reads (life 7a6e0005, memories 0006/0007,
// explanation 000b), mapped exactly like mobile `DeviceExportMapper`, so a
// board backup and an app backup of the same snapshot are the same payload.
// `sha256` is SHA-256 of the compact payload JSON in this key order; the
// golden fixture backend/tests/fixtures/xuna_export_v1_golden.json (produced
// by the Dart code) is checked in test_export_writer.
struct ExportSource {
  const uint8_t* life=nullptr;          // 20 B
  const uint8_t* memoryPages[2]={};     // 20 B each
  const uint8_t* explanation=nullptr;   // 20 B, or nullptr (old firmware: null)
  const uint8_t* petId=nullptr;         // 16 B
  uint8_t origin=0;                     // IdentityOrigin code 1..3
  uint32_t generation=0;
  int64_t exportedAtUtcMs=0;            // 0 = the board does not know the time
  const char* version="";               // e.g. "firmware/0.2.0-mvp"
  bool statedPreference=true,timeSync=true,identity=true;
};
class ExportWriter {
 public:
  static constexpr size_t kMaxPayload=1800,kMaxEnvelope=kMaxPayload+100,kMaxFrame=kMaxEnvelope+80;
  // Writes the compact payload into `out`; 0 if a byte is out of range or
  // the buffer is too small (nothing partial is meant to be sent).
  static size_t payload(const ExportSource& s,char* out,size_t cap) {
    Out o(out,cap);
    if(!s.life||!s.petId||!s.memoryPages[0]||!s.memoryPages[1])return 0;
    const uint8_t* l=s.life;
    if(l[0]!=1||l[1]!=20||l[2]>1||l[3]>3||l[16]>3||l[17]>3||l[18]>1||l[19]>8)return 0;
    const char* origin=originName(s.origin);
    if(!origin)return 0;
    char when[32];time(s.exportedAtUtcMs,when);
    o.add("{\"exportedAtUtc\":\"").add(when).add("\",\"app\":{\"version\":\"").add(s.version)
     .add("\"},\"capabilities\":{\"statedPreference\":").flag(s.statedPreference).add(",\"timeSync\":").flag(s.timeSync)
     .add(",\"identity\":").flag(s.identity).add("},\"identity\":{\"petId\":\"");
    for(int i=0;i<16;++i){char h[3];std::snprintf(h,sizeof(h),"%02x",s.petId[i]);o.add(h);}
    o.add("\",\"origin\":\"").add(origin).add("\",\"snapshotGeneration\":").num(s.generation)
     .add("},\"life\":{\"activeSeconds\":").num(get32(l+4)).add(",\"feedbackCount\":").num(get32(l+8))
     .add(",\"decisionId\":").num(get32(l+12)).add(",\"context\":\"").add(contexts(l[2]))
     .add("\",\"action\":\"").add(animation(l[3])).add("\",\"quietChoice\":\"").add(animation(l[16]))
     .add("\",\"playChoice\":\"").add(animation(l[17])).add("\",\"canFeedback\":").flag(l[18]==1)
     .add(",\"memoryCount\":").num(l[19]).add("},\"memories\":[");
    bool first=true;
    for(uint8_t page=0;page<2;++page){
      const uint8_t* m=s.memoryPages[page];
      if(m[0]!=1||m[1]>4||m[2]!=page||m[3]!=0)return 0;
      for(uint8_t i=0;i<m[1];++i){
        const uint8_t* e=m+4+4*i;
        if(e[0]>2||e[1]>3||e[2]>1||e[3]<1||e[3]>3)return 0;
        if(!first)o.add(",");
        first=false;
        o.add("{\"context\":\"").add(contexts(e[0])).add("\",\"action\":\"").add(animation(e[1]))
         .add("\",\"approved\":").flag(e[2]==1).add(",\"source\":\"").add(source(e[3])).add("\"}");
      }
    }
    o.add("],\"explanation\":");
    if(!s.explanation)o.add("null");
    else{
      const uint8_t* x=s.explanation;
      if(x[0]!=1||x[1]!=20||x[2]>2||x[3]>4||x[4]>2||x[5]>4||x[6]>1||get16(x+8)>256||
         get16(x+16)>get16(x+12)||get16(x+18)>get16(x+14))return 0;
      static const char* const kDayParts[]={"unknown","morning","afternoon","evening","night"};
      static const char* const kActions[]={"idle","happy","curious"};  // explanation codes, not animation codes
      static const char* const kReasons[]={"unavailable","highest_estimate","exploration","no_evidence","told"};
      o.add("{\"interaction\":\"").add(contexts(x[2])).add("\",\"dayPart\":\"").add(kDayParts[x[3]])
       .add("\",\"action\":\"").add(kActions[x[4]]).add("\",\"reason\":\"").add(kReasons[x[5]])
       .add("\",\"fine\":").flag(x[6]==1).add(",\"rated\":").num(x[7]).add(",\"probabilityQ8\":").num(get16(x+8))
       .add(",\"effectiveMassQ8\":").num(get16(x+10)).add(",\"positiveQ8\":").num(get16(x+12))
       .add(",\"negativeQ8\":").num(get16(x+14)).add(",\"statedPositiveQ8\":").num(get16(x+16))
       .add(",\"statedNegativeQ8\":").num(get16(x+18)).add("}");
    }
    o.add("}");
    return o.done();
  }
  // `{"format":"xuna.export.v1","sha256":"…","payload":…}` around a payload.
  static size_t envelope(const char* payloadJson,size_t n,char* out,size_t cap,char hashHex[65]) {
    uint8_t digest[32];Sha256::hash(reinterpret_cast<const uint8_t*>(payloadJson),n,digest);
    for(int i=0;i<32;++i)std::snprintf(hashHex+2*i,3,"%02x",digest[i]);
    Out o(out,cap);
    o.add("{\"format\":\"xuna.export.v1\",\"sha256\":\"").add(hashHex).add("\",\"payload\":").raw(payloadJson,n).add("}");
    return o.done();
  }
  // The WebSocket frame (contract "Device connection", DC-5).
  static size_t projectionFrame(uint32_t generation,const char* envelopeJson,size_t n,char* out,size_t cap) {
    Out o(out,cap);
    o.add("{\"type\":\"projection\",\"schemaVersion\":1,\"generation\":").num(generation).add(",\"export\":")
     .raw(envelopeJson,n).add("}");
    return o.done();
  }
  // RFC 3339 UTC with milliseconds, as Dart toIso8601String; "unknown" at 0.
  static void time(int64_t utcMs,char out[32]) {
    if(utcMs<=0){std::snprintf(out,32,"unknown");return;}
    const int64_t seconds=utcMs/1000;const int ms=int(utcMs%1000);
    int64_t days=seconds/86400;const int64_t rest=seconds%86400;
    // Days → civil date (H. Hinnant's algorithm).
    days+=719468;const int64_t era=days/146097;const int64_t doe=days-era*146097;
    const int64_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365;const int64_t doy=doe-(365*yoe+yoe/4-yoe/100);
    const int64_t mp=(5*doy+2)/153;const int day=int(doy-(153*mp+2)/5+1);const int month=int(mp<10?mp+3:mp-9);
    const int year=int(yoe+era*400+(month<=2));
    std::snprintf(out,32,"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",year,month,day,int(rest/3600),int(rest/60%60),
                  int(rest%60),ms);
  }
 private:
  class Out {
   public:
    Out(char* b,size_t cap):b_(b),cap_(cap){if(cap_)b_[0]=0;}
    Out& add(const char* s){return raw(s,std::strlen(s));}
    Out& raw(const char* s,size_t n){if(ok_&&n_+n<cap_){std::memcpy(b_+n_,s,n);n_+=n;b_[n_]=0;}else ok_=false;return *this;}
    Out& num(uint32_t v){char t[12];std::snprintf(t,sizeof(t),"%lu",static_cast<unsigned long>(v));return add(t);}
    Out& flag(bool v){return add(v?"true":"false");}
    size_t done() const {return ok_?n_:0;}
   private:
    char* b_;size_t cap_,n_=0;bool ok_=true;
  };
  static uint32_t get32(const uint8_t* b){return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
  static uint16_t get16(const uint8_t* b){return uint16_t(b[0]|(b[1]<<8));}
  static const char* contexts(uint8_t c){static const char* const k[]={"quiet","play","greeting"};return k[c];}
  static const char* animation(uint8_t a){static const char* const k[]={"idle","happy","sleep","curious"};return k[a];}
  static const char* source(uint8_t s){static const char* const k[]={"","button","text","reaction"};return k[s];}
  static const char* originName(uint8_t o) {
    switch(o){case 1:return "hatched_on_device";case 2:return "hatched_via_app";case 3:return "migrated_from_v1";default:return nullptr;}
  }
};
}
