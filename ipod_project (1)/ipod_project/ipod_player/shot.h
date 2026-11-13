#pragma once
#include <stdint.h>

// USB screen capture (debug): the PC sends 'S' at 115200 baud; the firmware
// freezes every animation, pauses the bot, switches to 921600 baud and streams
// the whole screen as RGB565 blocks; "\nOK\n" ends the frame.
// Nothing in normal operation touches this - zero cost unless used.
extern volatile bool gShotHold;

void shotPoll();          // call from idle loops: watches Serial for 'S'
void shotRequest();       // starts a dump (used internally)
void shotFlushHook(int x1, int y1, int x2, int y2, const void* px, uint32_t bytes);
