#pragma once
#include <Arduino.h>

void uiInit();                                   // styles + all screens (after displayInit)
void uiSplash(const char* line, int pct);        // boot / library loading screen
void uiIntro();                                  // short "Hello" boot animation (skippable)
void uiStart();                                  // show Now Playing
void uiLoop();                                   // buttons, animations, status (call every loop)
void uiToast(const char* text);                  // short message at the bottom
void uiLibraryChanged();
