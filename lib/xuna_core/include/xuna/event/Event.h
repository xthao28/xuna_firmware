#pragma once
#include <xuna/event/EventTypes.h>
#include <xuna/time/ClockModel.h>
namespace xuna {
enum class EventSource : uint8_t { DeviceInput,DeviceCore,MobileApp,MobileText,AiService,Sensor,Simulator };
enum class Provenance : uint8_t { DeviceObserved,SensorObserved,UserFeedback,UserStated,Inferred,GeneratedSummary,Simulated };
enum class PrivacyClass : uint8_t { LocalDevice,LocalPhone,Shareable };
// In-memory only. Never persist by memcpy: the versioned 40-byte codec belongs to S32.
struct Event {
 uint64_t eventId=0,causationId=0;
 uint32_t activeSecond=0,utcMinute=0; // minutes since 2020-01-01 UTC; zero unknown
 EventType type=EventType::BootStarted;
 EventSource source=EventSource::DeviceCore;
 Provenance provenance=Provenance::DeviceObserved;
 ClockQuality clockQuality=ClockQuality::Unknown;
 uint8_t confidence=0; // 255 means 1.0 per planned envelope
 PrivacyClass privacyClass=PrivacyClass::LocalDevice;
 uint8_t payload[8]={};
};
class EventIdGenerator {
 public:
  // Caller must supply a committed boot counter. Optional sequence restores/test-checks exhaustion.
  explicit EventIdGenerator(uint32_t bootCounter,uint32_t sequence=0):boot_(bootCounter),sequence_(sequence) {}
  bool next(uint64_t& id) {
    if(sequence_==UINT32_MAX)return false; // never wrap and reuse an event ID
    id=(uint64_t(boot_)<<32)|++sequence_;return true;
  }
 private:
  uint32_t boot_,sequence_;
};
}
