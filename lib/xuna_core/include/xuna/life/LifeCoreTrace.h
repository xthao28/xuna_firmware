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
          event = LifeCoreEvent(LifeCoreEventKind::RequestAction, 0, 0,
                                command.value0 == 'A' ? 1 : 0);
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
        case LearningTraceVerb::Sync:
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
      if (report.finalOutput.core.persistRequested) {
        ++report.persistRequests;
        if (!lab.persist()) ++report.persistFailures;
      }
    }
    report.finalEventCount = lab.session().core().life().eventCount;
    report.finalGeneration = lab.session().core().life().generation;
    report.fingerprint = fingerprint(lab.session());
    return true;
  }

 private:
  static uint32_t fingerprint(const LifeCoreSession& session) {
    uint8_t bytes[LifeEngine::kSnapshotSize] = {};
    session.core().life().encode(bytes);
    uint32_t hash = 2166136261U;
    for (uint8_t byte : bytes) hash = (hash ^ byte) * 16777619U;
    const uint32_t candidate = session.core().candidate().fingerprint();
    for (uint8_t i = 0; i < 4; ++i)
      hash = (hash ^ ((candidate >> (8U * i)) & 255U)) * 16777619U;
    hash = (hash ^ static_cast<uint8_t>(session.clock().quality(0))) *
           16777619U;
    return hash;
  }
};

}  // namespace xuna