#pragma once

#include <xuna/LifeEngine.h>

namespace xuna {

// Urgent-commit port (plan §9.5, §7.6). The core stages the v1 state with the
// feedback applied and asks the adapter to store it durably *before* applying
// it in RAM. The adapter may stamp the staged generation (production
// `LifeStorage::save(LifeEngine&)` does). Returning false means nothing was
// committed: the core keeps RAM unchanged, keeps the decision pending for a
// retry and reports not_ready (BLE result 7).
class ILifeCommit {
 public:
  virtual ~ILifeCommit() {}
  virtual bool commit(LifeEngine& staged) = 0;
};

}  // namespace xuna
