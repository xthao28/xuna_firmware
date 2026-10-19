#pragma once

#include <cstddef>
#include <cstdint>

#include <xuna/base/Sha256.h>
#include <xuna/learning/LearnerV3Explanation.h>
#include <xuna/learning/LearnerVocabulary.h>
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
  // S19a: `day` and `expect`.
  uint32_t simulatedDays = 0;
  uint32_t expectChecked = 0;
  uint32_t expectFailed = 0;
  uint32_t firstFailedCommand = 0;  // 1-based command index, 0 = none
  uint32_t firstFailedActual = 0;
  uint64_t finalMonoMs = 0;  // core 64-bit monotonic time since the last boot
  // SHA-256 of the encoded LifeSnapshot v2 (the bytes the A/B store keeps).
  uint8_t snapshotSha256[Sha256::kSize] = {};
  // Explanation of the last shown decision, as BLE READ 000b would project.
  V3Explanation explanation;
  LifeCoreSessionStepOutput finalOutput;
};

// Deterministic Life Lab runner (S19a) for the trace DSL. It drives the same
// LifeCoreSession used by adapters and the in-memory snapshot lab used by
// reboot/power-cut experiments. `day <n>` steps the core once per simulated
// minute; `expect <path> <op> <value>` checks state without stopping the run.
class LifeCoreTraceRunner {
 public:
  static constexpr size_t kMaxCommands = 64;
  // `day <n>` advances simulated time by n days in one-minute steps.
  static constexpr uint32_t kMaxDays = 3650;
  static constexpr uint32_t kDayStepMs = 60000;
  static constexpr uint64_t kDayMs = 86400000ULL;

  // `expect` paths (snake_case). Unknown paths make the trace invalid.
  static bool read(const char* path, const LifeCoreTraceReport& report,
                   const LifeCoreSession& session, uint32_t& value) {
    const LifeSnapshotV2& snapshot = session.core().snapshot();
    const LifeCoreSessionStepOutput& out = report.finalOutput;
    if (equals(path, "feedback_count")) value = snapshot.counters.feedbackCount;
    else if (equals(path, "generation")) value = snapshot.generation;
    else if (equals(path, "active_seconds")) value = snapshot.counters.activeSeconds;
    else if (equals(path, "activity")) value = uint32_t(out.core.activity);
    else if (equals(path, "visual")) value = uint32_t(out.core.visual);
    else if (equals(path, "has_decision")) value = out.core.hasDecision ? 1 : 0;
    else if (equals(path, "clock_quality")) value = uint32_t(out.clockQuality);
    else if (equals(path, "day_part")) value = uint32_t(out.dayPart);
    else if (equals(path, "preferred_quiet"))
      value = session.core().preferred(LearnInteraction::QuietRequest);
    else if (equals(path, "preferred_play"))
      value = session.core().preferred(LearnInteraction::PlayRequest);
    else if (equals(path, "preferred_greeting"))
      value = session.core().preferred(LearnInteraction::ButtonGreeting);
    else if (equals(path, "choices")) value = report.choices;
    else if (equals(path, "feedback_applied")) value = report.feedbackApplied;
    else if (equals(path, "persist_requests")) value = report.persistRequests;
    else if (equals(path, "persist_failures")) value = report.persistFailures;
    else if (equals(path, "reboots")) value = report.reboots;
    else if (equals(path, "sync_accepted")) value = report.syncAccepted;
    else if (equals(path, "sync_rejected")) value = report.syncRejected;
    else if (equals(path, "days")) value = report.simulatedDays;
    else return false;
    return true;
  }

  bool run(const LearningTraceDslCommand* commands, size_t count,
           LifeCoreTraceReport& report, uint64_t seed = 1,
           uint64_t stream = 3) const {
    report = LifeCoreTraceReport();
    if (count > kMaxCommands || (count != 0 && !commands)) return false;

    LifeCoreSnapshotLab lab(seed, stream);
    if (!lab.boot(0)) return false;
    uint32_t previous = 0;
    bool first = true;
    // Trace times are relative to the last `day`: `@+<ms>` stays a u32 while
    // 100-day runs pass millis() wrap (~49.7 days), as on the board.
    uint64_t dayOffset = 0;
    // millis() restarts at 0 on every reboot: the core sees `now - bootAt`.
    uint64_t bootAt = 0;
    V3Decision shown;
    for (size_t i = 0; i < count; ++i) {
      const LearningTraceDslCommand& command = commands[i];
      if (!first && command.atMs < previous) return false;
      first = false;
      previous = command.atMs;
      const uint64_t now = dayOffset + command.atMs;

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
                         TimeSource::Phone, MonoMs(now - bootAt))
                   : SyncResult{SyncStatus::RejectedInvalid, false};
          if (result.status == SyncStatus::Accepted) ++report.syncAccepted;
          else ++report.syncRejected;
          break;
        }
        case LearningTraceVerb::Day: {
          if (command.value0 == 0 || command.value0 > kMaxDays) {
            ++report.rejected;
            return false;
          }
          const uint64_t end = now + uint64_t(command.value0) * kDayMs;
          for (uint64_t t = now + kDayStepMs; t <= end; t += kDayStepMs)
            stepAt(lab, t - bootAt, report, shown);
          dayOffset += uint64_t(command.value0) * kDayMs;
          report.simulatedDays += command.value0;
          ++report.processed;
          continue;
        }
        case LearningTraceVerb::Expect: {
          uint32_t actual = 0;
          if (!read(command.text, report, lab.session(), actual)) {
            ++report.rejected;
            return false;
          }
          ++report.expectChecked;
          if (!compare(actual, command.op, command.value0)) {
            if (report.expectFailed++ == 0) {
              report.firstFailedCommand = uint32_t(i + 1);
              report.firstFailedActual = actual;
            }
          }
          ++report.processed;
          continue;
        }
        case LearningTraceVerb::Invalid:
          ++report.rejected;
          return false;
      }

      if (!supported) {
        ++report.unsupported;
      } else if (reboot) {
        bootAt = now;
        if (!lab.reboot(0)) {
          ++report.rejected;
          return false;
        }
        ++report.reboots;
      } else if (enqueue && !lab.session().enqueue(event)) {
        ++report.rejected;
        return false;
      }

      stepAt(lab, now - bootAt, report, shown);
      ++report.processed;
    }
    const LifeCoreSession& session = lab.session();
    report.finalEventCount = session.core().snapshot().counters.feedbackCount;
    report.finalGeneration = session.core().snapshot().generation;
    report.finalMonoMs = session.core().mono64();
    report.fingerprint = fingerprint(session);
    uint8_t bytes[SnapshotCodec::kBudget] = {};
    const size_t size =
        SnapshotCodec::encode(session.core().snapshot(), bytes, sizeof(bytes));
    Sha256::hash(bytes, size, report.snapshotSha256);
    report.explanation = LearnerV3Explainer::explain(session.core().learner(), shown);
    return true;
  }

 private:
  // One LifeCore step plus the committer bookkeeping. The lab is LifeCore's
  // committer (§9.5): feedback is committed before it is applied, and a
  // failed commit leaves RAM unchanged.
  static void stepAt(LifeCoreSnapshotLab& lab, uint64_t now,
                     LifeCoreTraceReport& report, V3Decision& shown) {
    report.finalOutput = lab.session().step(MonoMs(now));
    const LifeCoreStepOutput& core = report.finalOutput.core;
    if (core.learningFeedbackApplied) ++report.feedbackApplied;
    if (core.feedbackCommitted) ++report.persistRequests;
    if (core.feedbackNotReady) {
      ++report.persistRequests;
      ++report.persistFailures;
    }
    if (core.persistRequested) {
      ++report.persistRequests;
      if (!lab.persist()) ++report.persistFailures;
    }
    if (core.decisionShown) {
      // Same projection as V1LifeCoreAdapter's explanation decision.
      shown = V3Decision();
      shown.id = core.decisionId;
      shown.interaction = core.decisionContext;
      shown.dayPart = core.decisionDayPart;
      shown.explore = core.selectedExplore;
      if (!learnIndexFor(static_cast<ActionId>(core.selectedAction), shown.action))
        shown = V3Decision();
    }
  }

  static bool compare(uint32_t actual, const char* op, uint32_t expected) {
    if (equals(op, "==")) return actual == expected;
    if (equals(op, "!=")) return actual != expected;
    if (equals(op, ">=")) return actual >= expected;
    if (equals(op, "<=")) return actual <= expected;
    return false;
  }

  static bool equals(const char* first, const char* second) {
    size_t index = 0;
    while (first[index] != '\0' || second[index] != '\0') {
      if (first[index] != second[index]) return false;
      ++index;
    }
    return true;
  }

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