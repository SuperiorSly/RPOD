#pragma once
#include <Arduino.h>

enum BtnId : uint8_t {
  BTN_PLAY = 0,   // Play / Pause / Select   (IO22)
  BTN_BACK,       // Back                    (ladder)
  BTN_HOME,       // Home / menu             (IO27)
  BTN_VOLUP,      // Volume +                (ladder)
  BTN_VOLDN,      // Volume -                (ladder)
  BTN_UP,         // Up / seek / next        (ladder)
  BTN_DOWN,       // Down / seek back / prev (ladder)
  BTN_COUNT,
  BTN_NONE = 255
};

enum BtnEvtType : uint8_t { BE_PRESS, BE_RELEASE, BE_REPEAT };

struct BtnEvent {
  BtnId      id;
  BtnEvtType type;
  uint32_t   heldMs;   // how long the button has been down
};

void     buttonsInit();
void     buttonsPoll();                 // call as often as possible (every loop)
bool     buttonsGet(BtnEvent& e);       // pops one event, false if none
BtnId    buttonsDecodeLadder(uint32_t mv);
uint32_t buttonsReadLadderMv();         // for the hardware test
const char* buttonName(BtnId id);
