#pragma once
#include <Preferences.h>
#include <xuna/visual/Backlight.h>
#include <xuna/visual/SceneEffects.h>
namespace xuna {
// Owner's window scene and display power preferences. Display preferences,
// not pet life: their own NVS namespace `xuna_ui` (never `xuna_life`), written
// only when the owner changes them via GPIO14 hold or the companion app.
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
  BrightnessPreset loadBrightness() {
    Preferences prefs;
    if (!prefs.begin("xuna_ui", true)) return BrightnessPreset::Medium;
    const uint8_t value = prefs.getUChar("bright", static_cast<uint8_t>(BrightnessPreset::Medium));
    prefs.end();
    return brightnessPresetFrom(value);
  }
  bool saveBrightness(BrightnessPreset preset) {
    Preferences prefs;
    if (!prefs.begin("xuna_ui", false)) return false;
    const bool ok = prefs.putUChar("bright", static_cast<uint8_t>(preset)) == 1;
    prefs.end();
    return ok;
  }
  bool loadScreenSleep() {
    Preferences prefs;
    if (!prefs.begin("xuna_ui", true)) return true;
    const uint8_t value = prefs.getUChar("doze", 1);
    prefs.end();
    return value != 0;
  }
  bool saveScreenSleep(bool enabled) {
    Preferences prefs;
    if (!prefs.begin("xuna_ui", false)) return false;
    const bool ok = prefs.putUChar("doze", enabled ? 1 : 0) == 1;
    prefs.end();
    return ok;
  }
};
}
