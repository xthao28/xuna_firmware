#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/BleProtocol.h>
#include <xuna/learning/ExplanationCodec.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/LifeCore.h>
#include <xuna/persist/SnapshotStore.h>

namespace xuna {

// S13 + S18 integration seam (owner decision 2026-10-16): BLE protocol v1 on
// top of LifeCore + LifeSnapshot v2. It replaces the business logic in
// BleManager (v1 LifeEngine + BoardLearner) while keeping every v1 byte the
// mobile app parses: baseline result codes 0–7, the 20-byte life value and the two
// 20-byte memory pages. Pure C++11; the firmware supplies bytes, time and
// the commit port. Regression: the S01 observed transcript (test_v1_adapter).
//
// Intentional differences from the v1 path, all from approved decisions:
// - learner v3 (E10) chooses among idle/happy/curious (D5), so a choice or
//   the reported preference can be curious (ActionId 3, already in the app);
// - a rating later than 120 s after the reaction is rejected (D3):
//   baseline result 4, or dedicated result 8 with enhancedLearning (S26);
// - app decision IDs ≥ 0x80000000 are reserved for device decisions and
//   rejected with 4.
// GPIO0 stays a direct Happy request unless `greetingLearning` is enabled:
// v1 life/memory bytes cannot carry context 2, so the current app could not
// rate (and would fail to parse) a greeting decision.
class V1LifeCoreAdapter {
 public:
  enum Result : uint8_t {
    kOk = 0,
    kInvalidVersion = 1,
    kInvalidLength = 2,
    kUnknownOpcode = 3,
    kInvalidArgument = 4,
    kBusy = 5,
    kRequestConflict = 6,
    kNotReady = 7,
    kExpired = 8,
  };
  // TimeSync is the only v1 additive command wider than 12 bytes.
  static constexpr size_t kMaxPacket = 16;
  // v1 checkpoint cadence for active lifetime (main.cpp, 5 minutes).
  static constexpr uint32_t kCheckpointMs = 300000;

  V1LifeCoreAdapter(LifeCore& core, ISnapshotCommit& commit,
                    bool greetingLearning = false, bool enhancedLearning = false)
      : core_(core), commit_(commit), greetingLearning_(greetingLearning), enhancedLearning_(enhancedLearning) {
    core_.setCommitter(&commit_);
  }

  // After restore + LifeCore::begin.
  void begin(uint32_t now) {
    activeAt_ = savedAt_ = now;
    observe(core_.step(LifeCoreStepInput(now)));
  }

  // A BLE connect or disconnect: v1 clears the request cache and the
  // rateable app decision.
  void onSession() {
    cache_.clear();
    decision_ = 0;
    explanation_ = V3Decision();
  }

  // One inbound command write. `ready` = display/storage healthy (v1 sends
  // not_ready otherwise). Returns the result code; `id` is the request ID
  // echoed in the status value.
  uint8_t handle(const uint8_t* bytes, size_t size, uint32_t now, bool ready,
                 uint32_t& id) {
    const BleCommand c = parseCommand(bytes, size, enhancedLearning_, true);
    id = c.id;
    uint8_t result = c.error;
    const int duplicate = size <= kMaxPacket ? cache_.find(bytes, size) : -1;
    if (duplicate >= 0) return static_cast<uint8_t>(duplicate);
    if (!result && !ready) result = kNotReady;
    if (!result) result = dispatch(c, now);
    if (c.id) cache_.remember(bytes, size, result);
    return result;
  }

  // GPIO14: explicit idle. Accepted direct input invalidates the decision.
  bool localIdle(uint32_t now) {
    return requestAction(static_cast<uint8_t>(ActionId::Idle), now);
  }

  // GPIO0: learnable greeting (D5) when enabled, else the v1 Happy request.
  bool localGreeting(uint32_t now) {
    if (!greetingLearning_)
      return requestAction(static_cast<uint8_t>(ActionId::Happy), now);
    const LifeCoreEvent event(LifeCoreEventKind::Greeting);
    const LifeCoreStepOutput& out = step(event, now);
    if (out.decisionShown) decision_ = 0;
    return out.decisionShown;
  }

  // Offline quiet routine (LocalBehavior): show the learned quiet action.
  bool localAction(ActionId action, uint32_t now) {
    return requestAction(static_cast<uint8_t>(action), now);
  }

  // Every loop: advance the core and count active lifetime.
  const LifeCoreStepOutput& tick(uint32_t now) {
    observe(core_.step(LifeCoreStepInput(now)));
    const uint32_t seconds = uint32_t(now - activeAt_) / 1000U;
    core_.snapshot().counters.activeSeconds += seconds;
    activeAt_ += seconds * 1000U;
    return last_;
  }

  // Lazy checkpoint of active lifetime (plan §7.6), commit-first like the
  // urgent class: RAM adopts the staged copy only after it is durable.
  bool checkpointDue(uint32_t now) const {
    return uint32_t(now - savedAt_) >= kCheckpointMs;
  }
  bool checkpoint(uint32_t now) {
    if (core_.activity().state() == Activity::ErrorSafe) return false;
    savedAt_ = now;
    LifeSnapshotV2 staged = core_.snapshot();
    if (!commit_.commit(staged)) return false;
    core_.snapshot() = staged;
    return true;
  }

  // Learned preference for a v1 context (0 quiet, 1 play) as an ActionId.
  ActionId preferred(uint8_t context) const {
    ActionId action = ActionId::Idle;
    if (context > 1) return action;
    learnActionId(core_.preferred(static_cast<LearnInteraction>(context)),
                  action);
    return action;
  }

  // A projection only, not a new event or a physical action.
  void encodeExplanation(uint8_t* bytes) const {
    const auto projection=LearnerV3Explainer::explain(core_.learner(),explanation_);
    if(ExplanationCodec::encode(projection,bytes))return;
    for(size_t i=0;i<ExplanationCodec::kSize;++i)bytes[i]=0;
    bytes[0]=1;bytes[1]=ExplanationCodec::kSize;
  }

  bool canFeedback() const {
    return decision_ != 0 && last_.hasDecision && last_.decisionId == decision_;
  }

  // v1 life value (characteristic 7a6e0005, 20 bytes).
  void encodeLife(uint8_t* b) const {
    const LifeSnapshotV2& s = core_.snapshot();
    b[0] = 1;
    b[1] = 20;
    b[2] = context_;
    b[3] = action_;
    LifeEngine::put32(b + 4, s.counters.activeSeconds);
    LifeEngine::put32(b + 8, s.counters.feedbackCount);
    LifeEngine::put32(b + 12, decision_);
    b[16] = static_cast<uint8_t>(preferred(0));
    b[17] = static_cast<uint8_t>(preferred(1));
    b[18] = canFeedback() ? 1 : 0;
    b[19] = s.episodes.count;
  }

  // v1 memory page (characteristics 7a6e0006/7, 20 bytes): oldest first,
  // four episodes per page; actions as ActionIds.
  void encodeMemory(uint8_t page, uint8_t* b) const {
    const SnapshotEpisodes& e = core_.snapshot().episodes;
    for (uint8_t i = 0; i < 20; ++i) b[i] = 0;
    b[0] = 1;
    b[2] = page;
    const uint8_t oldest =
        e.count == SnapshotEpisodes::kCapacity ? e.next : 0;
    for (uint8_t i = 0; i < 4; ++i) {
      const uint8_t index = static_cast<uint8_t>(page * 4 + i);
      if (index >= e.count) break;
      const LifeEngine::Episode& episode =
          e.items[(oldest + index) % SnapshotEpisodes::kCapacity];
      ActionId action = ActionId::Idle;
      learnActionId(episode.action, action);
      b[4 + i * 4] = episode.context;
      b[5 + i * 4] = static_cast<uint8_t>(action);
      b[6 + i * 4] = episode.approved;
      b[7 + i * 4] = episode.source;
      ++b[1];
    }
  }

  // v1 status value (characteristic 7a6e0004, 16 bytes). kind 1 = command
  // result, 2 = state change.
  static void encodeStatus(uint8_t kind, uint32_t id, uint8_t result,
                           uint8_t petState, uint32_t sequence, uint32_t now,
                           uint8_t* b) {
    b[0] = 1;
    b[1] = kind;
    LifeEngine::put32(b + 2, id);
    b[6] = result;
    b[7] = petState;
    LifeEngine::put32(b + 8, sequence);
    LifeEngine::put32(b + 12, now);
  }

  const LifeCoreStepOutput& last() const { return last_; }
  VisualIntent visual() const { return last_.visual; }
  const ClockModel& clock() const { return core_.clock(); }
  uint64_t monoNow() const { return core_.mono64(); }

 private:
  uint8_t dispatch(const BleCommand& c, uint32_t now) {
    switch (c.opcode) {
      case 1:
        return requestAction(c.arg, now) ? kOk : kBusy;
      case 2: {
        if (c.id >= LifeCore::kLocalDecisionIdBase) return kInvalidArgument;
        const LifeCoreEvent event(LifeCoreEventKind::Choose, c.id, c.arg);
        const LifeCoreStepOutput& out = step(event, now);
        if (!out.decisionShown) return kBusy;
        // A rejected choice keeps the previous decision rateable (v1 rule).
        decision_ = c.id;
        context_ = c.arg;
        action_ = out.selectedAction;
        explanation_=V3Decision();explanation_.id=c.id;
        explanation_.interaction=c.arg;explanation_.dayPart=out.decisionDayPart;
        learnIndexFor(static_cast<ActionId>(action_),explanation_.action);
        explanation_.explore=out.selectedExplore;
        return kOk;
      }
      case 3: {
        if (c.target != decision_ || (!enhancedLearning_ && !canFeedback())) return kInvalidArgument;
        const LifeCoreEvent event(LifeCoreEventKind::Feedback, c.target, 0, 0,
                                  c.arg == 1, c.source);
        const LifeCoreStepOutput& out = step(event, now);
        if (out.learningFeedbackApplied) return kOk;
        if (out.feedbackNotReady) return kNotReady;
        if (enhancedLearning_ && out.feedbackExpired) return kExpired;
        return kInvalidArgument;  // expired (D3), consumed or rejected
      }
      case 4:
        decision_ = 0;
        if(!core_.forget())return kNotReady;
        explanation_=V3Decision();return kOk;
      case 5: {
        const int64_t utcMs = static_cast<int64_t>(c.utcSeconds) * 1000LL;
        const SyncResult result = core_.sync(
            utcMs, c.tzOffsetMin, TimeSource::Phone, now);
        return result.status == SyncStatus::Accepted ? kOk : kInvalidArgument;
      }
      case 6: {
        const LifeCoreEvent event(LifeCoreEventKind::StatedPreference,c.id,
                                  c.contextKey,c.action,c.arg==1,c.source);
        const auto& out=step(event,now);
        if(out.statedNotReady)return kNotReady;
        if(!out.statedApplied&&!out.statedDuplicate)return kInvalidArgument;
        explanation_=V3Decision();explanation_.id=c.id;
        explanation_.interaction=c.contextKey>>4;explanation_.dayPart=c.contextKey&15;
        learnIndexFor(static_cast<ActionId>(c.action),explanation_.action);
        return kOk;
      }
    }
    return kUnknownOpcode;
  }

  bool requestAction(uint8_t action, uint32_t now) {
    const LifeCoreEvent event(LifeCoreEventKind::RequestAction, 0, 0, action);
    const bool accepted = step(event, now).actionAccepted;
    if (accepted) {decision_ = 0;explanation_=V3Decision();}
    return accepted;
  }

  const LifeCoreStepOutput& step(const LifeCoreEvent& event, uint32_t now) {
    observe(core_.step(LifeCoreStepInput(now, &event, 1)));
    return last_;
  }

  void observe(const LifeCoreStepOutput& output) { last_ = output; }

  LifeCore& core_;
  ISnapshotCommit& commit_;
  bool greetingLearning_;
  bool enhancedLearning_;
  V3Decision explanation_;
  CommandCache cache_;
  LifeCoreStepOutput last_;
  uint32_t decision_ = 0;
  uint8_t context_ = 0;
  uint8_t action_ = 0;
  uint32_t activeAt_ = 0;
  uint32_t savedAt_ = 0;
};

}  // namespace xuna
