#pragma once
#include <cstdint>
#include <cstddef>
namespace xuna {
enum class EventCategory : uint8_t { Device,Time,Input,Behavior,Feedback,Learning,Memory,Growth,Evolution,Conversation,Safety };
enum class PersistClass : uint8_t { Ram,Log,Aggregate,ConditionalLog,Counter,LogSnapshot,LogJournalCandidate,MemoryStore,Tombstone,SnapshotJournal,CounterDiagnostic };
// Initial registry from implementation plan §7.3. Declaring a future kind does not enable it.
#define XUNA_EVENT_TYPES(X) \
 X(BootStarted,0x01,Device,Log,false) \
 X(TimeSynchronized,0x02,Time,Log,false) \
 X(ClockRejected,0x03,Time,Log,false) \
 X(DayPartChanged,0x04,Time,Ram,false) \
 X(InteractionSessionStarted,0x10,Input,Log,false) \
 X(ButtonInteraction,0x11,Input,Aggregate,false) \
 X(BehaviorChosen,0x20,Behavior,ConditionalLog,false) \
 X(RequestRejected,0x21,Behavior,Counter,false) \
 X(FeedbackApplied,0x30,Feedback,LogSnapshot,true) \
 X(FeedbackRejected,0x31,Feedback,Counter,false) \
 X(PreferenceShifted,0x40,Learning,LogJournalCandidate,false) \
 X(HabitPredictionScored,0x41,Learning,Aggregate,false) \
 X(FactConfirmed,0x50,Memory,MemoryStore,true) \
 X(FactSuperseded,0x51,Memory,MemoryStore,true) \
 X(FactDeleted,0x52,Memory,Tombstone,true) \
 X(MemoryForgotten,0x53,Memory,Log,true) \
 X(PatternConsolidated,0x54,Memory,MemoryStore,false) \
 X(GrowthEligibilityReached,0x60,Growth,Log,false) \
 X(GrowthStageChanged,0x61,Growth,SnapshotJournal,true) \
 X(SkillAcquired,0x62,Growth,SnapshotJournal,true) \
 X(TraitUnlocked,0x70,Evolution,SnapshotJournal,true) \
 X(TraitConsolidated,0x71,Evolution,SnapshotJournal,true) \
 X(ConversationSessionEnded,0x80,Conversation,Aggregate,false) \
 X(StorageError,0xF0,Safety,CounterDiagnostic,false) \
 X(EventsDropped,0xF1,Safety,Counter,false) \
 X(SafeModeEntered,0xF2,Safety,Log,true)
#define XUNA_TYPE(name,code,category,persist,critical) name=code,
enum class EventType : uint8_t { XUNA_EVENT_TYPES(XUNA_TYPE) };
#undef XUNA_TYPE
struct EventTypeInfo { EventType type;EventCategory category;PersistClass persistence;bool critical; };
#define XUNA_INFO(name,code,category,persist,critical) {EventType::name,EventCategory::category,PersistClass::persist,critical},
constexpr EventTypeInfo kEventTypes[]={XUNA_EVENT_TYPES(XUNA_INFO)};
#undef XUNA_INFO
#undef XUNA_EVENT_TYPES
constexpr size_t kEventTypeCount=sizeof(kEventTypes)/sizeof(kEventTypes[0]);
constexpr bool eventCodesUnique(size_t i=0,size_t j=1) {
 return i>=kEventTypeCount?true:(j>=kEventTypeCount?eventCodesUnique(i+1,i+2):
 (kEventTypes[i].type!=kEventTypes[j].type&&eventCodesUnique(i,j+1)));
}
static_assert(eventCodesUnique(),"Event type codes must be unique");
inline const EventTypeInfo* eventInfo(EventType type) {
 for(const auto& info:kEventTypes)if(info.type==type)return &info;
 return nullptr;
}
}
