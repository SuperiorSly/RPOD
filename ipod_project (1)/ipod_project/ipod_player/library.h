#pragma once
#include <Arduino.h>

struct Song {
  uint32_t id;          // unique per file (hash of name + size)
  uint16_t durSec;      // length in seconds
  uint8_t  hasCover;    // 1 = cover + thumbnail files exist on the SD card
  uint8_t  _pad;
  char     path[80];    // "/music/xxx.mp3"
  char     title[40];
  char     artist[32];
};

typedef void (*ScanProgressCb)(int done, int total, const char* name);

void        libraryInit();                       // allocate memory
void        libraryScan(ScanProgressCb cb);      // read /music, build/refresh index
int         libraryCount();
const Song& librarySong(int i);
int         libraryFindById(uint32_t id);

// Heavy work (ID3 + cover decode). Safe to call from another task.
bool        libraryProcessFile(const char* path, Song& out);
// Add a processed song to the list (call from the main task only).
bool        libraryAppend(const Song& s);
void        librarySaveIndex();

// "S:/ipod/covers/xxxxxxxx.bin" (LVGL path). thumb=true -> small version.
void        libraryCoverPath(const Song& s, char* out, size_t n, bool thumb);
void        fmtTime(uint32_t sec, char* out, size_t n);   // m:ss
