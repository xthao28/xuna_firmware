#pragma once

#include <cstddef>
#include <cstdint>
#include <xuna/life/ActivityFsm.h>
#include <xuna/visual/VisualIntent.h>

namespace xuna {

enum class ActionId : uint8_t { Idle = 0, Happy = 1, Sleep = 2, Curious = 3 };
enum class ParamType : uint8_t { None = 0, Integer = 1 };
enum class PermissionTier : uint8_t {
  Public = 0,
  OwnerBonded = 1,
  OwnerButtonConfirm = 2,
  NeverFromAi = 3
};

struct ParamSpec {
  ParamType type;
  int16_t min;
  int16_t max;
  uint8_t maxLen;
};

struct ActionDescriptor {
  ActionId actionId;
  uint8_t nameId;
  ParamSpec params;
  uint8_t minStage;
  uint32_t cooldownMs;
  uint16_t energyCost;
  bool requiresBond;
  PermissionTier permission;
  VisualIntent visualIntent;
};

// M04 public action vocabulary. IDs are stable protocol-facing values.
static const ActionDescriptor kActionDescriptors[] = {
    {ActionId::Idle, 0, {ParamType::None, 0, 0, 0}, 0, 0, 0, false,
     PermissionTier::Public, VisualIntent::Idle},
    {ActionId::Happy, 1, {ParamType::None, 0, 0, 0}, 0, 1500, 4096, false,
     PermissionTier::Public, VisualIntent::Happy},
    {ActionId::Sleep, 2, {ParamType::None, 0, 0, 0}, 0, 5000, 0, false,
     PermissionTier::Public, VisualIntent::Sleep},
    {ActionId::Curious, 3, {ParamType::None, 0, 0, 0}, 0, 1500, 2048, false,
     PermissionTier::Public, VisualIntent::Curious}};

constexpr size_t kActionDescriptorCount =
    sizeof(kActionDescriptors) / sizeof(kActionDescriptors[0]);

inline const ActionDescriptor* findAction(uint8_t id) {
  for (size_t i = 0; i < kActionDescriptorCount; ++i) {
    if (static_cast<uint8_t>(kActionDescriptors[i].actionId) == id)
      return &kActionDescriptors[i];
  }
  return 0;
}

inline Activity activityForAction(ActionId action) {
  switch (action) {
    case ActionId::Idle: return Activity::Idle;
    case ActionId::Happy:
    case ActionId::Curious: return Activity::Reacting;
    case ActionId::Sleep: return Activity::Sleeping;
  }
  return Activity::ErrorSafe;
}

}  // namespace xuna