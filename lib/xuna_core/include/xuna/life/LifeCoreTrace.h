#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/life/LifeCoreSnapshotLab.h>
#include <xuna/learning/LearningTraceDsl.h>

namespace xuna {

struct LifeCoreTraceReport {
  uint32_t processed = 0;
  uint32_t unsupported = 0;
  uint32_t rejected = 0;
  uint32_t choices = 0;
  uint32_t feedbackApplied = 0;
  uint32_t persistRequests = 0;
  uint32_t persistFailures = 0;
  uint32_t reboots = 0;
  uint32_t syncAccepted = 0;
  uint32_t syncRejected = 0;
  uint32_t finalEventCount = 0;
  uint32_t finalGeneration = 0;
  uint32_t fingerprint = 0;
  LifeCoreSessionStepOutput finalOutput;
};

// Deterministic runner for the core-facing subset of the DSL. It drives the
// same LifeCoreSession used by adapters and the in-memory snapshot lab used by
// reboot/power-cut experiments; unsupported verbs are counted explicitly.
class LifeCoreTraceRunner {
 public:
  static constexpr size_t kMaxCommands = 64;

  bool run(const LearningTraceDslCommand* commands, size_t count,
           LifeCoreTraceReport& report, uint64_t seed = 1,
           uint64_t stream = 3) const {
    report = LifeCoreTraceReport();
    if (count > kMaxCommands || (count != 0 && !commands)) return false;

    LifeCoreSnapshotLab lab(seed, stream);
    if (!lab.boot(0)) return false;
    uint32_t previous = 0;
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
      const LearningTraceDslCommand& command = commands[i];
      if (!first && command.atMs < previous) return false;
      first = false;
      previous = command.atMs;

      LifeCoreEvent event;
      bool supported = true;
      bool enqueue = false;
      bool reboot = false;
      switch (command.verb) {
        case LearningTraceVerb::Choose:
          if (command.value0 == 0 || command.value1 > 1) {
            ++report.rejected;
            return false;
          }
          event = LifeCoreEvent(LifeCoreEventKind::Choose, command.value0,
                                static_cast<uint8_t>(command.value1));
          ++report.choices;
          enqueue = true;
          break;
        case LearningTraceVerb::Feedback:
          if (command.value0 == 0 || command.value1 > 2) {
            ++report.rejected;
            return false;
          }
          event = LifeCoreEvent(LifeCoreEventKind::Feedback, command.value0,
                                0, 0, command.flag,
                                static_cast<uint8_t>(command.value1));
          enqueue = true;
          break;
        case LearningTraceVerb::Button:
          // GPIO0 ('A') is the learnable greeting (D5); GPIO14 ('B') is a
          // direct idle request.
          event = command.value0 == 'A'
                      ? LifeCoreEvent(LifeCoreEventKind::Greeting)
                      : LifeCoreEvent(LifeCoreEventKind::RequestAction, 0, 0,
                                      static_cast<uint8_t>(ActionId::Idle));
          enqueue = true;
          break;
        case LearningTraceVerb::Command:
          if (command.value0 > 3) {
            ++report.rejected;
            return false;
          }
          event = LifeCoreEvent(LifeCoreEventKind::RequestAction, 0, 0,
                                static_cast<uint8_t>(command.value0));
          enqueue = true;
          break;
        case LearningTraceVerb::Reboot:
          reboot = true;
          break;
        case LearningTraceVerb::Powercut:
          lab.powerCutNextPersist();
          break;
        case LearningTraceVerb::Sync: {
          // `sync <unix seconds> <tz minutes>` from the phone (S10).
          const bool tzOk = command.signedValue >= -32768 &&
                            command.signedValue <= 32767;
          const SyncResult result =
              tzOk ? lab.session().sync(
                         int64_t(command.value0) * 1000,
                         static_cast<int16_t>(command.signedValue),
                         TimeSource::Phone, command.atMs)
                   : SyncResult{SyncStatus::RejectedInvalid, false};
          if (result.status == SyncStatus::Accepted) ++report.syncAccepted;
          else ++report.syncRejected;
          break;
        }
        case LearningTraceVerb::Day:
        case LearningTraceVerb::Expect:
          supported = false;
          break;
        case LearningTraceVerb::Invalid:
          ++report.rejected;
          return false;
      }

      if (!supported) {
        ++report.unsupported;
      } else if (reboot) {
        if (!lab.reboot(command.atMs)) {
          ++report.rejected;
          return false;
        }
        ++report.reboots;
      } else if (enqueue && !lab.session().enqueue(event)) {
        ++report.rejected;
        return false;
      }

      report.finalOutput = lab.session().step(command.atMs);
      ++report.processed;
      if (report.finalOutput.core.learningFeedbackApplied)
        ++report.feedbackApplied;
      // The lab is LifeCore's committer (§9.5): feedback is committed before
      // it is applied, and a failed commit leaves RAM unchanged.
      const LifeCoreStepOutput& core = report.finalOutput.core;
      if (core.feedbackCommitted) ++report.persistRequests;
      if (core.feedbackNotReady) {
        ++report.persistRequests;
        ++report.persistFailures;
      }
      if (core.persistRequested) {
        ++report.persistRequests;
        if (!lab.persist()) ++report.persistFailures;
      }
    }
    report.finalEventCount =
        lab.session().core().snapshot().counters.feedbackCount;
    report.finalGeneration = lab.session().core().snapshot().generation;
    report.fingerprint = fingerprint(lab.session());
    return true;
  }

 private:
  static uint32_t fingerprint(const LifeCoreSession& session) {
    uint8_t bytes[SnapshotCodec::kBudget] = {};
    const size_t size =
        SnapshotCodec::encode(session.core().snapshot(), bytes, sizeof(bytes));
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619U;
    hash = (hash ^ static_cast<uint8_t>(session.clock().quality(0))) *
           16777619U;
    return hash;
  }
};

}  // namespace xuna