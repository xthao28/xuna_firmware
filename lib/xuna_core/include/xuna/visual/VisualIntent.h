#pragma once

#include <cstddef>
#include <cstdint>

namespace xuna {

enum class VisualIntent : uint8_t {
  Idle = 0,
  Happy,
  Sleep,
  Curious,
  Rest,
  ErrorSafe,
  Celebrate,
  Greet,
  Unknown = 255
};

// Track A (2026-10-24): short expressive variants played over the current
// pose (option A: expression only; the learner still picks idle/happy/curious).
// Order matches the asset (tools/generate_track_a.py). Append only.
enum class VisualVariant : uint8_t {
  None = 0,
  LookAround,   // explore drive / anticipating the owner (loop)
  YawnStretch,  // going to rest or waking up (one-shot)
  BouncePlay,   // play drive behind a happy choice (loop)
  GreetNuzzle,  // the owner is back after a while (one-shot)
};
constexpr uint8_t kVisualVariantCount = 4;

struct ExpressionMap {
  VisualIntent intent;
  uint8_t minStage;
  uint8_t poseId;
  VisualIntent fallback;
};

struct ExpressionResolution {
  VisualIntent requested;
  VisualIntent resolved;
  uint8_t poseId;
  bool usedFallback;
};

static const ExpressionMap kExpressionMap[] = {
    {VisualIntent::Idle, 0, 0, VisualIntent::Idle},
    {VisualIntent::Happy, 0, 1, VisualIntent::Idle},
    {VisualIntent::Sleep, 0, 2, VisualIntent::Idle},
    {VisualIntent::Curious, 0, 3, VisualIntent::Idle},
    {VisualIntent::Rest, 0, 4, VisualIntent::Idle},
    {VisualIntent::ErrorSafe, 0, 5, VisualIntent::Idle},
    {VisualIntent::Celebrate, 1, 6, VisualIntent::Happy},
    {VisualIntent::Greet, 2, 7, VisualIntent::Happy}};

constexpr size_t kExpressionMapCount =
    sizeof(kExpressionMap) / sizeof(kExpressionMap[0]);

inline const ExpressionMap* findExpression(VisualIntent intent) {
  for (size_t i = 0; i < kExpressionMapCount; ++i) {
    if (kExpressionMap[i].intent == intent) return &kExpressionMap[i];
  }
  return 0;
}

inline ExpressionResolution resolveVisualIntent(VisualIntent requested,
                                                uint8_t stage) {
  ExpressionResolution result{requested, VisualIntent::Idle, 0, true};
  VisualIntent current = requested;
  for (size_t depth = 0; depth <= kExpressionMapCount; ++depth) {
    const ExpressionMap* entry = findExpression(current);
    if (!entry) return result;
    if (stage >= entry->minStage) {
      result.resolved = current;
      result.poseId = entry->poseId;
      result.usedFallback = current != requested;
      return result;
    }
    current = entry->fallback;
  }
  return result;
}

}  // namespace xuna