#pragma once

#include <cstdint>

#include <xuna/life/ActionRegistry.h>
#include <xuna/life/ActivityFsm.h>
#include <xuna/life/Needs.h>

namespace xuna {

// Life-feel configuration chosen in Life Lab against the owner-approved
// targets (2026-10-15). Approved for a board trial; values remain provisional
// until the owner's board check. See
// docs/roadmap/learning/life-lab-needs-timeline-2026-10-15.md.
inline LifeTuning lifeFeelTuning() {
  LifeTuning tuning;
  tuning.energyRecoveryPerMinute = 6144;
  tuning.actionCostPercent = 10;
  tuning.stimulationPerInteraction = 4096;
  return tuning;
}

inline ActivityThresholds lifeFeelThresholds() {
  ActivityThresholds thresholds;
  thresholds.restExit = 41943;    // rest until about 64% energy
  thresholds.wakeEnergy = 52428;  // wake by itself at about 80% energy
  thresholds.resumeInterruptedRest = true;
  return thresholds;
}

// Shared effect of an accepted action on needs, used by LifeCore and the
// production BLE/button path so both behave the same.
inline void applyAcceptedAction(Needs& needs, uint8_t action) {
  const LifeTuning& tuning = needs.tuning();
  if (tuning.actionCostPercent) {
    const ActionDescriptor* descriptor = findAction(action);
    if (descriptor)
      needs.spend(static_cast<uint16_t>(uint32_t(descriptor->energyCost) *
                                        tuning.actionCostPercent / 100U));
  }
  if (tuning.stimulationPerInteraction)
    needs.stimulate(tuning.stimulationPerInteraction);
}

}  // namespace xuna
