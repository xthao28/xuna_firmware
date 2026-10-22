#pragma once
#include <Preferences.h>
#include <xuna/visual/SceneEffects.h>
namespace xuna {
// Owner's window scene choice. A display preference, not pet life: its own
// NVS namespace `xuna_ui` (never `xuna_life`), written only when the owner
// changes it with a GPIO14 hold.
class ScenePreference {
 public:
  SceneMode load() {
    Preferences prefs;
    if (!prefs.begin("xuna_ui", true)) return SceneMode::Auto;  // namespace not created yet
    const uint8_t value = prefs.getUChar("scene", 0);
    prefs.end();
    return sceneModeFrom(value);
  }
  bool save(SceneMode mode) {
    Preferences prefs;
    if (!prefs.begin("xuna_ui", false)) return false;
    const bool ok = prefs.putUChar("scene", static_cast<uint8_t>(mode)) == 1;
    prefs.end();
    return ok;
  }
};
}
