#pragma once
#include <Arduino.h>
#include "library.h"

enum TgPhase : uint8_t { TG_OFF = 0, TG_CONNECTING, TG_WAITING, TG_DOWNLOADING, TG_PROCESSING, TG_ERROR, TG_STOPPING };

struct TgStatus {
  TgPhase  phase;
  uint8_t  pct;            // download progress
  char     line[56];       // file name / error text
  char     last[56];       // last song added
  uint16_t added;          // songs added this session
};

void     telegramStart();                 // WiFi on + start the bot worker
void     telegramStop();                  // asks the worker to stop (non-blocking)
bool     telegramActive();                // WiFi/bot worker still alive
TgStatus telegramStatus();
bool     telegramTakeSong(Song& out);     // a freshly processed song for the main task
