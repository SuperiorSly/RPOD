#pragma once
#include <Arduino.h>

enum AudioState : uint8_t { AS_IDLE = 0, AS_PLAYING, AS_PAUSED };

void       audioInit();                                   // start the audio task
void       audioPlay(const char* path, uint32_t startMs, uint32_t durSec);
void       audioPause();
void       audioResume();
void       audioStop();
void       audioSeekMs(uint32_t ms);                      // absolute position
void       audioSetVolume(uint8_t pct);                   // 0..100
AudioState audioState();
uint32_t   audioPosMs();
bool       audioTakeEnded();                              // true once after a song finished
// Hardware test: 0 off, 1 = 440 Hz tone, 2 = DC low (0 V), 3 = DC mid (1.65 V), 4 = DC high (3.3 V)
void       audioTestMode(uint8_t mode);
