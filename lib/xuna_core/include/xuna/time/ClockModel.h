#pragma once
#include <cstdint>
#include <limits>
namespace xuna {
enum class ClockQuality : uint8_t { Unknown,Approximate,Synced };
enum class TimeSource : uint8_t { Unknown,Phone,Ntp };
enum class DayPart : uint8_t { Unknown,Morning,Afternoon,Evening,Night };
enum class SyncStatus : uint8_t { Accepted,RejectedRollback,RejectedInvalid };
struct SyncResult { SyncStatus status;bool gap; };
// Pure model: persisted watermark detects rollback, never reconstructs offline time.
class ClockModel {
 public:
  // UTC API remains Unix milliseconds; compact minutes use the 2020 epoch.
  static constexpr int64_t kEpochUtcMs=1577836800000LL;
  static constexpr uint64_t kValidityMs=24ULL*60*60*1000;
  static constexpr int64_t kRollbackToleranceMs=5LL*60*1000;
  void onBoot(uint64_t mono=0,uint32_t lastTrustedMinute=0) {
    anchored_=false;anchorMono_=mono;anchorUtc_=0;offset_=0;
    source_=TimeSource::Unknown;lastTrusted_=lastTrustedMinute;
  }
  SyncResult onSync(int64_t utc,int16_t offset,TimeSource source,uint64_t mono) {
    if(utc<kEpochUtcMs+60000||(utc-kEpochUtcMs)/60000>UINT32_MAX||offset< -840||offset>840||
       (source!=TimeSource::Phone&&source!=TimeSource::Ntp)||mono<anchorMono_)
      return {SyncStatus::RejectedInvalid,false};
    const int64_t projected=utcMs(mono);
    const int64_t watermark=lastTrusted_?kEpochUtcMs+int64_t(lastTrusted_)*60000:0;
    const int64_t reference=projected>watermark?projected:watermark;
    if(reference>utc&&reference-utc>kRollbackToleranceMs)
      return {SyncStatus::RejectedRollback,false};
    const bool gap=reference!=0&&utc>reference&&utc-reference>kRollbackToleranceMs;
    anchorUtc_=utc<reference?reference:utc;anchorMono_=mono;
    offset_=offset;source_=source;anchored_=true;
    const uint32_t minute=static_cast<uint32_t>((anchorUtc_-kEpochUtcMs)/60000);
    if(minute>lastTrusted_)lastTrusted_=minute;
    return {SyncStatus::Accepted,gap};
  }
  ClockQuality quality(uint64_t mono) const {
    if(!utcMs(mono))return ClockQuality::Unknown;
    return mono-anchorMono_>kValidityMs?ClockQuality::Approximate:ClockQuality::Synced;
  }
  int64_t utcMs(uint64_t mono) const {
    if(!anchored_||mono<anchorMono_)return 0;
    const uint64_t elapsed=mono-anchorMono_;
    if(elapsed>uint64_t(std::numeric_limits<int64_t>::max()-anchorUtc_))return 0;
    const int64_t now=anchorUtc_+static_cast<int64_t>(elapsed);
    return (now-kEpochUtcMs)/60000>UINT32_MAX?0:now;
  }
  uint32_t utcMinute(uint64_t mono) const {
    const int64_t utc=utcMs(mono);
    return utc?static_cast<uint32_t>((utc-kEpochUtcMs)/60000):0;
  }
  DayPart dayPart(uint64_t mono) const {
    const int64_t utc=utcMs(mono);if(!utc)return DayPart::Unknown;
    const int64_t localMinute=utc/60000+offset_;
    const int minute=static_cast<int>((localMinute%1440+1440)%1440);
    if(minute<300||minute>=1320)return DayPart::Night;
    if(minute<660)return DayPart::Morning;
    if(minute<1020)return DayPart::Afternoon;
    return DayPart::Evening;
  }
  uint16_t localDayIndex(uint64_t mono) const {
    if (quality(mono) == ClockQuality::Unknown) return 0;
    const int64_t utc = utcMs(mono);
    if (utc < kEpochUtcMs) return 0;
    const int64_t localMinute = (utc - kEpochUtcMs) / 60000 + offset_;
    if (localMinute < 0) return 0;
    const int64_t day = localMinute / 1440 + 1;
    return static_cast<uint16_t>(day > 65535 ? 65535 : day);
  }
  uint32_t lastTrustedUtcMinute() const { return lastTrusted_; }
  int16_t tzOffsetMin() const { return offset_; }
  TimeSource source() const { return source_; }
 private:
  bool anchored_=false;
  uint64_t anchorMono_=0;
  int64_t anchorUtc_=0;
  uint32_t lastTrusted_=0;
  int16_t offset_=0;
  TimeSource source_=TimeSource::Unknown;
};
}
