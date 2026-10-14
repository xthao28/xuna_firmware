#pragma once

#include <cstdint>
#include <xuna/life/ActionRegistry.h>
#include <xuna/life/Needs.h>

namespace xuna {

enum class ValidationReason : uint8_t {
  Accepted = 0,
  UnknownAction,
  InvalidParameter,
  Stage,
  Activity,
  Cooldown,
  Energy,
  Permission,
  Busy,
  InvalidContext
};

struct ActionRequest {
  uint8_t actionId;
  int16_t parameter;
  ActionRequest(uint8_t id = 0, int16_t value = 0)
      : actionId(id), parameter(value) {}
};

struct ValidatorContext {
  Activity activity = Activity::Idle;
  uint8_t stage = 0;
  uint16_t energy = 65535;
  uint32_t now = 0;
  bool busy = false;
  PermissionTier caller = PermissionTier::Public;
  uint8_t acceptedMask = 0;
  uint32_t lastAcceptedAt[4] = {};
};

struct ValidationResult {
  bool allowed = false;
  ValidationReason reason = ValidationReason::UnknownAction;
  ActionId action = ActionId::Idle;
  Activity target = Activity::ErrorSafe;
  VisualIntent visual = VisualIntent::Idle;
};

class RequestValidator {
 public:
  static ValidationResult validate(const ActionRequest& request,
                                   const ValidatorContext& context) {
    ValidationResult result;
    const ActionDescriptor* descriptor = findAction(request.actionId);
    if (!descriptor) return result;
    result.action = descriptor->actionId;
    result.target = activityForAction(descriptor->actionId);
    result.visual = descriptor->visualIntent;
    result.reason = ValidationReason::InvalidContext;
    if (context.busy) {
      result.reason = ValidationReason::Busy;
      return result;
    }
    if (descriptor->params.type == ParamType::None && request.parameter != 0) {
      result.reason = ValidationReason::InvalidParameter;
      return result;
    }
    if (descriptor->params.type == ParamType::Integer &&
        (request.parameter < descriptor->params.min ||
         request.parameter > descriptor->params.max)) {
      result.reason = ValidationReason::InvalidParameter;
      return result;
    }
    if (context.stage < descriptor->minStage) {
      result.reason = ValidationReason::Stage;
      return result;
    }
    if (descriptor->requiresBond ||
        static_cast<uint8_t>(context.caller) <
            static_cast<uint8_t>(descriptor->permission) ||
        descriptor->permission == PermissionTier::NeverFromAi) {
      result.reason = ValidationReason::Permission;
      return result;
    }
    const bool noOp = context.activity == result.target &&
                      (result.target == Activity::Idle ||
                       result.target == Activity::Sleeping);
    if (!noOp && !ActivityFsm::legal(context.activity, result.target)) {
      result.reason = ValidationReason::Activity;
      return result;
    }
    const uint8_t id = static_cast<uint8_t>(descriptor->actionId);
    if (id >= 4) {
      result.reason = ValidationReason::InvalidContext;
      return result;
    }
    if ((context.acceptedMask & (uint8_t(1) << id)) &&
        uint32_t(context.now - context.lastAcceptedAt[id]) <
            descriptor->cooldownMs) {
      result.reason = ValidationReason::Cooldown;
      return result;
    }
    if (context.energy < descriptor->energyCost) {
      result.reason = ValidationReason::Energy;
      return result;
    }
    result.allowed = true;
    result.reason = ValidationReason::Accepted;
    return result;
  }
};

}  // namespace xuna
