#pragma once

#include <cstdint>

#include <xuna/learning/LearningShadow.h>
#include <xuna/learning/PrequentialEvaluator.h>

namespace xuna {

enum class LearningTraceKind : uint8_t { Choose = 1, Feedback = 2 };

struct LearningTraceEvent {
  LearningTraceKind kind = LearningTraceKind::Choose;
  uint32_t at = 0;
  uint32_t decisionId = 0;
  uint8_t context = 0;
  bool approved = false;
  uint8_t source = 0;

  LearningTraceEvent() {}
  LearningTraceEvent(LearningTraceKind kindValue, uint32_t atValue,
                     uint32_t decisionIdValue, uint8_t contextValue,
                     bool approvedValue, uint8_t sourceValue)
      : kind(kindValue),
        at(atValue),
        decisionId(decisionIdValue),
        context(contextValue),
        approved(approvedValue),
        source(sourceValue) {}
};

struct LearningTraceReport {
  uint32_t processed = 0;
  uint32_t unknownFeedback = 0;
  uint32_t invalidEvents = 0;
  LearningEvaluation evaluation;
  ShadowMetrics shadow;
  uint32_t fingerprint = 0;
};

// Small deterministic runner for host tests and the future Life Lab. It keeps
// only the most recent eight decisions, matching the bounded feedback window.
class LearningTraceRunner {
 public:
  static constexpr uint8_t kMaxEvents = 64;
  static constexpr uint8_t kDecisionWindow = 8;

  bool run(const LearningTraceEvent* events, uint8_t count,
           LearningTraceReport& report, uint64_t seed = 1,
           uint64_t stream = 3) const {
    report = LearningTraceReport();
    if (events == nullptr || count > kMaxEvents) {
      ++report.invalidEvents;
      return false;
    }

    LearningShadow shadow(seed, stream);
    PrequentialEvaluator evaluator;
    TraceSlot slots[kDecisionWindow] = {};
    uint8_t nextSlot = 0;

    for (uint8_t index = 0; index < count; ++index) {
      const LearningTraceEvent& event = events[index];
      ++report.processed;
      if (event.kind == LearningTraceKind::Choose) {
        if (event.decisionId == 0) {
          ++report.invalidEvents;
          continue;
        }
        TraceSlot& slot = slots[nextSlot];
        nextSlot = static_cast<uint8_t>((nextSlot + 1U) % kDecisionWindow);
        slot.decision = shadow.choose(event.context, event.decisionId, event.at);
        slot.valid = slot.decision.candidate.id != 0;
        slot.feedbackApplied = false;
        if (!slot.valid) ++report.invalidEvents;
        continue;
      }

      if (event.kind != LearningTraceKind::Feedback) {
        ++report.invalidEvents;
        continue;
      }
      TraceSlot* slot = find(slots, event.decisionId);
      if (slot == nullptr || !slot->valid) {
        ++report.unknownFeedback;
        continue;
      }
      if (event.source < 1 || event.source > 2) {
        // Counted as rejected by the shadow; never scored or applied.
        shadow.feedback(slot->decision, event.approved, event.source, event.at);
        continue;
      }
      if (!slot->feedbackApplied &&
          !evaluator.score(shadow.candidate(), slot->decision.candidate,
                           event.approved)) {
        ++report.invalidEvents;
        continue;
      }
      if (shadow.feedback(slot->decision, event.approved, event.source,
                          event.at)) {
        slot->feedbackApplied = true;
      }
    }

    report.evaluation = evaluator.metrics();
    report.shadow = shadow.metrics();
    report.fingerprint = shadow.fingerprint();
    return report.invalidEvents == 0;
  }

 private:
  struct TraceSlot {
    ShadowDecision decision;
    bool valid = false;
    bool feedbackApplied = false;
  };

  static TraceSlot* find(TraceSlot* slots, uint32_t decisionId) {
    if (decisionId == 0) return nullptr;
    for (uint8_t index = 0; index < kDecisionWindow; ++index) {
      if (slots[index].valid &&
          slots[index].decision.candidate.id == decisionId) {
        return &slots[index];
      }
    }
    return nullptr;
  }
};

}  // namespace xuna
