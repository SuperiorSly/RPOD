#pragma once
#include <Arduino.h>

struct Settings {
  uint8_t  theme      = 0;     // index into the theme table
  uint8_t  brightness = 80;    // percent (BRIGHT_MIN_PCT..100)
  uint8_t  volume     = 30;    // percent
  bool     shuffle    = false;
  bool     bluetooth  = false; // reserved (see README)
  uint32_t lastSongId = 0;
};
extern Settings gSettings;

void settingsLoad();
void settingsDirty();        // call after changing gSettings; saved a bit later
void settingsLoop();         // call from loop()
void settingsSaveNow();
