#pragma once
#include <xuna/identity/IdentityRecord.h>
namespace xuna {
struct IdentityEntropy { uint8_t petId[16]={};uint32_t seed=0; };
enum class IdentityState : uint8_t { Egg,NeedsMigration,Ready,Safe };
// Pure preparation only. Caller obtains real entropy and commits before acceptPersisted.
class IdentityService {
 public:
  void onBoot(uint32_t boot,IdentityLoad load,const IdentityRecord* record,bool validV1Snapshot) {
    boot_=boot;record_=IdentityRecord();state_=IdentityState::Safe;
    if(!boot)return;
    if(load==IdentityLoad::Valid){if(record&&record->valid()){record_=*record;state_=IdentityState::Ready;}return;}
    if(load==IdentityLoad::Missing)state_=validV1Snapshot?IdentityState::NeedsMigration:IdentityState::Egg;
  }
  IdentityState state() const { return state_; }
  const IdentityRecord& record() const { return record_; }
  bool prepareMigration(const IdentityEntropy& entropy,uint32_t active,uint64_t mono,IdentityRecord& out) const {
    if(state_!=IdentityState::NeedsMigration)return false;
    out=make(entropy,IdentityOrigin::MigratedFromV1,active,mono);return true;
  }
  bool prepareHatch(const IdentityEntropy& entropy,IdentityOrigin origin,uint32_t active,uint64_t mono,
                    const ClockModel& clock,IdentityRecord& out) const {
    if(state_!=IdentityState::Egg||(origin!=IdentityOrigin::HatchedOnDevice&&origin!=IdentityOrigin::HatchedViaApp))return false;
    out=make(entropy,origin,active,mono);
    if(clock.quality(mono)==ClockQuality::Synced){out.bornAtUtcMs=clock.utcMs(mono);out.bornAtQuality=ClockQuality::Synced;}
    return true;
  }
  bool prepareBornAt(const ClockModel& clock,uint64_t mono,IdentityRecord& out) const {
    if(state_!=IdentityState::Ready||record_.bornAtUtcMs||record_.origin==IdentityOrigin::MigratedFromV1||
       record_.hatchBootCounter!=boot_||mono<record_.hatchMonoMs||clock.quality(mono)!=ClockQuality::Synced)return false;
    const int64_t utc=clock.utcMs(mono);const uint64_t elapsed=mono-record_.hatchMonoMs;
    if(elapsed>uint64_t(utc-ClockModel::kEpochUtcMs))return false;
    out=record_;out.bornAtUtcMs=utc-static_cast<int64_t>(elapsed);out.bornAtQuality=ClockQuality::Synced;return true;
  }
  bool acceptPersisted(const IdentityRecord& candidate) {
    if(state_==IdentityState::Safe||!candidate.valid())return false;
    if(state_==IdentityState::Ready){
      if(std::memcmp(candidate.petId,record_.petId,16)||candidate.seed!=record_.seed||
         candidate.hatchBootCounter!=record_.hatchBootCounter||candidate.hatchActiveSecond!=record_.hatchActiveSecond||
         candidate.hatchMonoMs!=record_.hatchMonoMs||candidate.hatchContentVersion!=record_.hatchContentVersion||candidate.origin!=record_.origin)return false;
      if(candidate.bornAtUtcMs!=record_.bornAtUtcMs&&(record_.bornAtUtcMs||record_.hatchBootCounter!=boot_))return false;
    }else if(candidate.hatchBootCounter!=boot_||
      (state_==IdentityState::NeedsMigration)!=(candidate.origin==IdentityOrigin::MigratedFromV1))return false;
    record_=candidate;state_=IdentityState::Ready;return true;
  }
 private:
  IdentityRecord make(const IdentityEntropy& entropy,IdentityOrigin origin,uint32_t active,uint64_t mono) const {
    IdentityRecord out;std::memcpy(out.petId,entropy.petId,16);out.petId[6]=(out.petId[6]&15)|0x40;out.petId[8]=(out.petId[8]&63)|0x80;
    out.seed=entropy.seed;out.origin=origin;out.hatchBootCounter=boot_;out.hatchActiveSecond=active;out.hatchMonoMs=mono;return out;
  }
  uint32_t boot_=0;IdentityState state_=IdentityState::Safe;IdentityRecord record_;
};

// S10 protocol freeze: 3-second single-button hold on GPIO14 in Egg hatches
// the pet on the device; releasing early or pressing GPIO0 cancels cleanly.
struct EggHatchHoldOutput {
  bool holding = false;
  bool started = false;
  bool stepChanged = false;
  bool canceled = false;
  bool triggered = false;
  uint8_t step = 0;  // 0: 0–1s, 1: 1–2s, 2: 2–3s, 3: 3s completed
};

class EggHatchHold {
 public:
  static constexpr uint32_t kHoldMs = 3000;

  EggHatchHoldOutput update(bool inEgg, bool idlePressed, bool happyPressed, uint32_t now) {
    EggHatchHoldOutput out;
    if (!inEgg || !idlePressed || happyPressed) {
      if (holding_ && !fired_) out.canceled = true;
      if (!idlePressed) fired_ = false;
      holding_ = false;
      step_ = 0;
      return out;
    }
    if (fired_) return out;
    if (!holding_) {
      holding_ = true;
      startedAt_ = now;
      step_ = 0;
      out.holding = true;
      out.started = true;
      return out;
    }
    const uint32_t elapsed = uint32_t(now - startedAt_);
    if (elapsed >= kHoldMs) {
      fired_ = true;
      step_ = 3;
      out.step = 3;
      out.triggered = true;
      return out;
    }
    const uint8_t nextStep = elapsed >= 2000U ? 2U : (elapsed >= 1000U ? 1U : 0U);
    if (nextStep != step_) {
      step_ = nextStep;
      out.stepChanged = true;
    }
    out.holding = true;
    out.step = step_;
    return out;
  }

 private:
  uint32_t startedAt_ = 0;
  uint8_t step_ = 0;
  bool holding_ = false;
  bool fired_ = false;
};
}
