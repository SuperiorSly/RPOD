#include "settings.h"
#include "config.h"
#include <Preferences.h>

Settings gSettings;
static Preferences prefs;
static bool dirty = false;
static uint32_t dirtyAt = 0;

void settingsLoad() {
  prefs.begin("ipod", true);
  gSettings.theme      = prefs.getUChar("theme", 0);
  gSettings.brightness = prefs.getUChar("bright", 80);
  gSettings.volume     = prefs.getUChar("vol", AUDIO_DEFAULT_VOLUME);
  gSettings.shuffle    = prefs.getBool("shuf", false);
  gSettings.bluetooth  = prefs.getBool("bt", false);
  gSettings.lastSongId = prefs.getUInt("last", 0);
  prefs.end();
  if (gSettings.brightness < BRIGHT_MIN_PCT) gSettings.brightness = BRIGHT_MIN_PCT;
  if (gSettings.brightness > 100) gSettings.brightness = 100;
  if (gSettings.volume > 100) gSettings.volume = 100;
}

void settingsSaveNow() {
  prefs.begin("ipod", false);
  prefs.putUChar("theme", gSettings.theme);
  prefs.putUChar("bright", gSettings.brightness);
  prefs.putUChar("vol", gSettings.volume);
  prefs.putBool("shuf", gSettings.shuffle);
  prefs.putBool("bt", gSettings.bluetooth);
  prefs.putUInt("last", gSettings.lastSongId);
  prefs.end();
  dirty = false;
}

void settingsDirty() { dirty = true; dirtyAt = millis(); }

void settingsLoop() {
  if (dirty && millis() - dirtyAt > 2000) settingsSaveNow();
}
