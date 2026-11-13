#pragma once
#include <Arduino.h>
#include <SD.h>

bool     sdBegin();                 // mount the card; false = not readable
bool     sdReady();
void     sdLock();                  // the SD card is shared by 3 tasks -> lock
void     sdUnlock();
uint64_t sdFreeBytes();
uint64_t sdTotalBytes();
void     sdRegisterLvgl();          // lets LVGL read "S:/path/file.bin" from SD

struct SdGuard {
  SdGuard()  { sdLock(); }
  ~SdGuard() { sdUnlock(); }
};
