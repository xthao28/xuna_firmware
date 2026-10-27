#pragma once

#include <cstddef>
#include <cstdint>

namespace xuna {

enum class IdentityWireState : uint8_t { Egg=0, Ready=1, Safe=2 };
enum class GrowthWireStage : uint8_t { Egg=0, Newborn=1 };
enum class ClockWireQuality : uint8_t { Unknown=0, Approximate=1, Synced=2 };
enum class BirthWireQuality : uint8_t { Unknown=0, Synced=2 };
enum class IdentityWireOrigin : uint8_t { None=0, Device=1, App=2, Migrated=3 };

struct LifecycleProjection {
  IdentityWireState identity=IdentityWireState::Egg;
  GrowthWireStage stage=GrowthWireStage::Egg;
  ClockWireQuality clock=ClockWireQuality::Unknown;
  BirthWireQuality birth=BirthWireQuality::Unknown;
  IdentityWireOrigin origin=IdentityWireOrigin::None;
  bool hasBornAt=false;
};

class LifecycleCodec {
 public:
  static constexpr size_t kSize=8;

  static bool encode(const LifecycleProjection& value,uint8_t* out) {
    if(!out||!valid(value))return false;
    out[0]=1;out[1]=kSize;out[2]=static_cast<uint8_t>(value.identity);
    out[3]=static_cast<uint8_t>(value.stage);
    out[4]=static_cast<uint8_t>(value.clock);
    out[5]=static_cast<uint8_t>(value.birth);
    out[6]=static_cast<uint8_t>(value.origin);out[7]=value.hasBornAt?1:0;
    return true;
  }

 private:
  static bool valid(const LifecycleProjection& value) {
    const uint8_t identity=static_cast<uint8_t>(value.identity);
    const uint8_t stage=static_cast<uint8_t>(value.stage);
    const uint8_t clock=static_cast<uint8_t>(value.clock);
    const uint8_t birth=static_cast<uint8_t>(value.birth);
    const uint8_t origin=static_cast<uint8_t>(value.origin);
    return identity<=2&&stage<=1&&clock<=2&&(birth==0||birth==2)&&origin<=3&&
      value.hasBornAt==(birth==2);
  }
};

}  // namespace xuna
