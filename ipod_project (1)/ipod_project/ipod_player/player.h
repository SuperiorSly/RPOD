#pragma once
#include <Arduino.h>

enum PlayerState : uint8_t { PS_STOPPED = 0, PS_PLAYING, PS_PAUSED };

void        playerInit();                 // after the library is loaded
void        playerLoop();                 // call often (detects end of song)
int         playerCurrent();              // library index, -1 = none
PlayerState playerState();
void        playerPlayIndex(int idx);     // start this song from the beginning
void        playerToggle();               // play / pause (starts the current song if stopped)
void        playerNext();
void        playerPrev();
void        playerSeekRel(int32_t deltaMs);
uint32_t    playerPosMs();
uint32_t    playerDurSec();
void        playerSetVolume(int pct);
void        playerSetShuffle(bool on);
void        playerLibraryChanged();       // a song was added
void        playerStopAll();              // e.g. before WiFi mode
bool        playerTakeChanged();          // true once after the song changed
