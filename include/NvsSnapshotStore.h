#pragma once
#include <Preferences.h>
#include <xuna/identity/IdentityRecord.h>
#include <xuna/persist/SnapshotStore.h>
namespace xuna {
// D2a (owner 2026-10-16): LifeSnapshot v2 in the existing NVS partition,
// namespace `xuna_life`, A/B keys `life0/life1`. The v1 keys `slot0/slot1`
// are only read (migration) and stay untouched for one release as the
// rollback point. Identity (64 B) and the boot counter use their own keys.
// Production store for the merged S13 + S18 switch.
class NvsSnapshotStore : public ISnapshotStore {
  Preferences prefs_;
  bool ready_=false;
  static const char* name(SnapshotKey key) {
    switch(key) {
      case SnapshotKey::V1Slot0: return "slot0";
      case SnapshotKey::V1Slot1: return "slot1";
      case SnapshotKey::V2Slot0: return "life0";
      case SnapshotKey::V2Slot1: return "life1";
    }
    return "";
  }
 public:
  bool begin() { ready_=prefs_.begin("xuna_life",false); return ready_; }
  bool ready() const { return ready_; }
  bool read(SnapshotKey key,uint8_t* out,size_t capacity,size_t& size) override {
    const char* k=name(key);
    if(!ready_||!prefs_.isKey(k))return false;
    size=prefs_.getBytesLength(k);
    // An oversized value is reported by size only; recovery treats it as corrupt.
    if(size<=capacity&&prefs_.getBytes(k,out,size)!=size)size=capacity+1;
    return true;
  }
  bool write(SnapshotKey key,const uint8_t* bytes,size_t size) override {
    if(!ready_||key==SnapshotKey::V1Slot0||key==SnapshotKey::V1Slot1)return false;
    return prefs_.putBytes(name(key),bytes,size)==size;
  }
  // Identity record (S06 codec, 64 B): Missing, or the raw bytes to decode.
  IdentityLoad readIdentity(IdentityRecord& out) {
    if(!ready_||!prefs_.isKey("ident"))return IdentityLoad::Missing;
    uint8_t b[IdentityRecord::kEncodedSize];
    if(prefs_.getBytesLength("ident")!=sizeof(b)||prefs_.getBytes("ident",b,sizeof(b))!=sizeof(b))
      return IdentityLoad::Corrupt;
    return out.decode(b,sizeof(b));
  }
  bool writeIdentity(const IdentityRecord& record) {
    uint8_t b[IdentityRecord::kEncodedSize];
    return ready_&&record.encode(b)&&prefs_.putBytes("ident",b,sizeof(b))==sizeof(b);
  }
  // Boot counter `bootc` (plan §7.1): incremented and committed at every boot.
  uint32_t nextBoot() {
    if(!ready_)return 0;
    const uint32_t boot=prefs_.getUInt("bootc",0)+1;
    return prefs_.putUInt("bootc",boot)==sizeof(uint32_t)?boot:0;
  }
};
}
