#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/BleProtocol.h>
#include <xuna/LifecycleCodec.h>
#include <xuna/identity/IdentityService.h>
#include <xuna/learning/ExplanationCodec.h>
#include <xuna/learning/LearnerVocabulary.h>
#include <xuna/life/BootSequence.h>
#include <xuna/life/LifeCore.h>
#include <xuna/persist/SnapshotStore.h>
#include <xuna/personality/PersonalityProjection.h>
#include <xuna/knowledge/KnowledgeProjection.h>

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
  // Opcode 1 args 4–7: Track A variant preview (enhanced adapter only).
  static constexpr uint8_t kVariantPreviewBase = 4;
  // Wire order, not the enum order (explicit so a reorder cannot change it).
  static VisualVariant previewVariant(uint8_t arg) {
    switch (arg) {
      case 4: return VisualVariant::LookAround;
      case 5: return VisualVariant::YawnStretch;
      case 6: return VisualVariant::BouncePlay;
      case 7: return VisualVariant::GreetNuzzle;
      default: return VisualVariant::None;
    }
  }
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

  // S10 hatch & same-boot bornAt backfill: wires identity persistence and RNG.
  void attachIdentity(IdentityService* identity, IBootStore* store, IRandom* entropy) {
    identity_ = identity;
    identityStore_ = store;
    entropy_ = entropy;
  }

  // S10 commit-first hatch (device GPIO14 3s hold or BLE opcode 7).
  uint8_t hatch(IdentityOrigin origin) {
    if (!identity_ || !identityStore_ || !entropy_) return kNotReady;
    if (identity_->state() == IdentityState::Safe) return kNotReady;
    if (identity_->state() != IdentityState::Egg) return kInvalidArgument;
    IdentityEntropy fresh;
    for (size_t i = 0; i < 4; ++i)
      LifeEngine::put32(fresh.petId + i * 4, entropy_->next());
    fresh.seed = entropy_->next();
    IdentityRecord candidate;
    if (!identity_->prepareHatch(fresh, origin, activeSeconds(), core_.mono64(),
                                 core_.clock(), candidate))
      return kInvalidArgument;
    if (!identityStore_->writeIdentity(candidate) ||
        !identity_->acceptPersisted(candidate))
      return kNotReady;
    core_.seedPersonality(identity_->record().seed);
    core_.previewVariant(VisualVariant::GreetNuzzle);
    return kOk;
  }

  bool hatchOnDevice() { return hatch(IdentityOrigin::HatchedOnDevice) == kOk; }

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
    const BleCommand c = parseCommand(bytes, size, enhancedLearning_, true, enhancedLearning_,
                                        enhancedLearning_, enhancedLearning_, enhancedLearning_);
    id = c.id;
    uint8_t result = c.error;
    const int duplicate = size <= kMaxPacket ? cache_.find(bytes, size) : -1;
    if (duplicate >= 0) return static_cast<uint8_t>(duplicate);
    if (!result && !ready) result = kNotReady;
    if (!result) result = dispatch(c, now);
    if (c.id) cache_.remember(bytes, size, result);
    return result;
  }

  // GPIO14: explicit idle. Right after XUNA acted it is also a "shoo"
  // reaction (D6 revised 2026-10-19) that teaches before the idle request.
  // Accepted direct input invalidates the decision.
  bool localIdle(uint32_t now) {
    reactionApplied_ = false;
    if (core_.reactionOpen(now)) react(false, now);
    return requestAction(static_cast<uint8_t>(ActionId::Idle), now);
  }

  // GPIO0: right after XUNA acted it is a "pet" reaction; otherwise the
  // learnable greeting (D5) when enabled, else the v1 Happy request.
  bool localGreeting(uint32_t now) {
    reactionApplied_ = false;
    if (core_.reactionOpen(now)) return react(true, now);
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

  // Offline routine with learning: XUNA chooses on its own in the quiet
  // context, and the owner may react. Not app-rateable (decision_ cleared).
  bool localSpontaneous(uint32_t now) {
    const LifeCoreEvent event(LifeCoreEventKind::Spontaneous);
    const LifeCoreStepOutput& out = step(event, now);
    if (out.decisionShown) decision_ = 0;
    return out.decisionShown;
  }

  // True when the last button press taught XUNA (pet or shoo).
  bool lastReactionApplied() const { return reactionApplied_; }

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

  // Personality projection (7a6e000e, PE-3); `seeded` = the pet has an identity.
  void encodePersonality(bool seeded, uint8_t* bytes) const {
    PersonalityProjection::encode(core_.personality(), seeded, bytes);
  }

  // Knowledge projection (7a6e000f, KD-2, 64 bytes).
  void encodeKnowledge(uint8_t* bytes) const {
    const bool trusted = core_.clock().quality(core_.mono64()) != ClockQuality::Unknown;
    KnowledgeProjection::encode(core_.knowledge(), trusted, bytes);
  }
  const KnowledgeProfile& knowledge() const { return core_.knowledge(); }

  // Lifecycle V1 projection (7a6e0013, S10, 8 bytes).
  void encodeLifecycle(uint8_t* bytes) const {
    LifecycleProjection p;
    p.clock = static_cast<ClockWireQuality>(core_.clock().quality(core_.mono64()));
    if (identity_) {
      switch (identity_->state()) {
        case IdentityState::Egg:
        case IdentityState::NeedsMigration:
          p.identity = IdentityWireState::Egg;
          p.stage = GrowthWireStage::Egg;
          break;
        case IdentityState::Ready: {
          const IdentityRecord& r = identity_->record();
          p.identity = IdentityWireState::Ready;
          p.stage = GrowthWireStage::Newborn;
          p.birth = r.bornAtQuality == ClockQuality::Synced
                        ? BirthWireQuality::Synced
                        : BirthWireQuality::Unknown;
          p.origin = static_cast<IdentityWireOrigin>(r.origin);
          p.hasBornAt = p.birth == BirthWireQuality::Synced;
          break;
        }
        case IdentityState::Safe:
          p.identity = IdentityWireState::Safe;
          p.stage = GrowthWireStage::Egg;
          break;
      }
    }
    if (!LifecycleCodec::encode(p, bytes)) {
      for (size_t i = 0; i < LifecycleCodec::kSize; ++i) bytes[i] = 0;
      bytes[0] = 1;
      bytes[1] = LifecycleCodec::kSize;
    }
  }

  // Last durable snapshot generation (Identity V1 projection ordering key).
  uint32_t generation() const { return core_.snapshot().generation; }
  uint32_t activeSeconds() const { return core_.snapshot().counters.activeSeconds; }

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

  // Power Phase 2: returns true once per accepted opcode 11 DisplaySettings command.
  bool takeDisplaySettings(uint8_t& brightness, bool& screenSleep) {
    if (!displaySettingsPending_) return false;
    displaySettingsPending_ = false;
    brightness = displayBrightness_;
    screenSleep = displayScreenSleep_;
    return true;
  }

 private:
  uint8_t dispatch(const BleCommand& c, uint32_t now) {
    switch (c.opcode) {
      case 1:
        if (c.arg >= kVariantPreviewBase) {
          core_.previewVariant(previewVariant(c.arg));
          return kOk;
        }
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
        if (result.status != SyncStatus::Accepted) return kInvalidArgument;
        if (identity_ && identityStore_) {
          IdentityRecord updated;
          if (identity_->prepareBornAt(core_.clock(), core_.mono64(), updated)) {
            if (!identityStore_->writeIdentity(updated) ||
                !identity_->acceptPersisted(updated))
              return kNotReady;
          }
        }
        return kOk;
      }
      case 7:
        return hatch(IdentityOrigin::HatchedViaApp);
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
      case 9: {
        // PE-4d: arg 1 undo the newest change, 2 reset. Nothing to undo is
        // invalid_argument; a failed commit is not_ready and changes nothing.
        switch (core_.controlPersonality(c.arg == 2)) {
          case LifeCore::PersonalityControlResult::Applied: return kOk;
          case LifeCore::PersonalityControlResult::Nothing: return kInvalidArgument;
          case LifeCore::PersonalityControlResult::NotReady: return kNotReady;
        }
        return kNotReady;
      }
      case 8: {
        // PE-4c: ok also when the daily cap ignores it; the app never retries.
        const LifeCoreEvent event(LifeCoreEventKind::Conversation, c.id, 0, c.arg);
        step(event, now);
        return kOk;
      }
      case 10: {
        switch (core_.handleKnowledge(c.arg, c.slot, c.slot2, c.factKey, now)) {
          case LifeCore::KnowledgeCommandResult::Applied: return kOk;
          case LifeCore::KnowledgeCommandResult::Duplicate: return kOk;
          case LifeCore::KnowledgeCommandResult::Invalid: return kInvalidArgument;
          case LifeCore::KnowledgeCommandResult::NotReady: return kNotReady;
        }
        return kInvalidArgument;
      }
      case 11: {
        displaySettingsPending_ = true;
        displayBrightness_ = c.arg;
        displayScreenSleep_ = c.source != 0;
        return kOk;
      }
    }
    return kUnknownOpcode;
  }

  bool react(bool approved, uint32_t now) {
    const LifeCoreEvent event(LifeCoreEventKind::Reaction, 0, 0, 0, approved);
    reactionApplied_ = step(event, now).reactionApplied;
    if (reactionApplied_) {decision_ = 0;explanation_=V3Decision();}
    return reactionApplied_;
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
  bool reactionApplied_ = false;
  uint8_t context_ = 0;
  uint8_t action_ = 0;
  uint32_t activeAt_ = 0;
  uint32_t savedAt_ = 0;
  bool displaySettingsPending_ = false;
  uint8_t displayBrightness_ = 1;
  bool displayScreenSleep_ = true;
  IdentityService* identity_ = nullptr;
  IBootStore* identityStore_ = nullptr;
  IRandom* entropy_ = nullptr;
};

}  // namespace xuna
