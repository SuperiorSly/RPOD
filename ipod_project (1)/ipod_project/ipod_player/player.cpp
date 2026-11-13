#include "player.h"
#include "audio.h"
#include "library.h"
#include "settings.h"
#include "config.h"

static int          cur = -1;
static PlayerState  st = PS_STOPPED;
static uint16_t     order[MAX_SONGS];
static int          orderN = 0, orderPos = 0;
static bool         changed = false;
static int          failStreak = 0;
static uint32_t     seekTarget = 0, seekAt = 0;
static uint32_t     startPosMs = 0;

static void buildOrder(int first) {
  orderN = libraryCount();
  for (int i = 0; i < orderN; i++) order[i] = i;
  orderPos = (first >= 0 && first < orderN) ? first : 0;
  if (gSettings.shuffle && orderN > 1) {
    for (int i = orderN - 1; i > 0; i--) { int j = esp_random() % (i + 1); uint16_t t = order[i]; order[i] = order[j]; order[j] = t; }
    if (first >= 0) {
      for (int i = 0; i < orderN; i++) if (order[i] == first) { order[i] = order[0]; order[0] = first; break; }
      orderPos = 0;
    }
  }
}

void playerInit() {
  buildOrder(-1);
  audioSetVolume(gSettings.volume);
  int i = gSettings.lastSongId ? libraryFindById(gSettings.lastSongId) : -1;
  if (i < 0 && libraryCount() > 0) i = 0;
  cur = i; st = PS_STOPPED; changed = true;
  if (cur >= 0) buildOrder(cur);
}

int         playerCurrent() { return cur; }
PlayerState playerState()   { return st; }

void playerPlayIndex(int idx) {
  if (idx < 0 || idx >= libraryCount()) return;
  if (idx != cur || orderN != libraryCount()) buildOrder(idx);
  else for (int i = 0; i < orderN; i++) if (order[i] == idx) { orderPos = i; break; }
  const Song& s = librarySong(idx);
  cur = idx;
  audioPlay(s.path, 0, s.durSec);
  st = PS_PLAYING; changed = true; startPosMs = 0; seekAt = 0;
  gSettings.lastSongId = s.id; settingsDirty();
}

void playerToggle() {
  if (cur < 0) { if (libraryCount() > 0) playerPlayIndex(0); return; }
  if (st == PS_PLAYING)      { audioPause();  st = PS_PAUSED; }
  else if (st == PS_PAUSED)  { audioResume(); st = PS_PLAYING; }
  else                       { playerPlayIndex(cur); }
}

void playerNext() {
  int n = libraryCount(); if (!n) return;
  if (orderN != n) buildOrder(cur);
  if (++orderPos >= orderN) { if (gSettings.shuffle) buildOrder(-1); orderPos = 0; }
  playerPlayIndex(order[orderPos]);
}

void playerPrev() {
  int n = libraryCount(); if (!n) return;
  if (st != PS_STOPPED && playerPosMs() > 3000) { playerPlayIndex(cur); return; }
  if (orderN != n) buildOrder(cur);
  if (--orderPos < 0) orderPos = orderN - 1;
  playerPlayIndex(order[orderPos]);
}

uint32_t playerDurSec() { return cur >= 0 ? librarySong(cur).durSec : 0; }

uint32_t playerPosMs() {
  if (st == PS_STOPPED) return 0;
  if (seekAt && millis() - seekAt < 400) return seekTarget;
  return audioPosMs();
}

void playerSeekRel(int32_t d) {
  if (cur < 0 || st == PS_STOPPED) return;
  int64_t t = (int64_t)playerPosMs() + d;
  int64_t mx = (int64_t)playerDurSec() * 1000 - 1000;
  if (t > mx) t = mx;
  if (t < 0) t = 0;
  seekTarget = (uint32_t)t; seekAt = millis();
  audioSeekMs(seekTarget);
}

void playerSetVolume(int pct) {
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  gSettings.volume = pct; settingsDirty();
  audioSetVolume(pct);
}

void playerSetShuffle(bool on) {
  gSettings.shuffle = on; settingsDirty();
  buildOrder(cur);
}

void playerLibraryChanged() {
  if (cur < 0 && libraryCount() > 0) { cur = 0; changed = true; }
  buildOrder(cur);
}

void playerStopAll() {
  audioStop(); st = PS_STOPPED;
}

bool playerTakeChanged() { bool c = changed; changed = false; return c; }

void playerLoop() {
  if (audioTakeEnded() && st != PS_STOPPED) {
    uint32_t p = audioPosMs();
    failStreak = (p < 500) ? failStreak + 1 : 0;      // song ended instantly = unreadable file
    if (failStreak > libraryCount() + 1) { st = PS_STOPPED; failStreak = 0; changed = true; return; }
    playerNext();
  }
}
