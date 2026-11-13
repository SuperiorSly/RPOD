// ============================================================================
//  shot.cpp - USB screen capture (debug aid)
//  Protocol:
//    PC -> 'S'              (device is at 115200)
//    device -> "BLK" + u16le x,y,w,h + w*h*2 bytes of RGB565, repeated until
//              the full 240x320 frame is out (blocks arrive top-to-bottom),
//              then "\nOK\n" and back to 115200.
//  While a dump runs: every LVGL animation is deleted (world frozen) and the
//  Telegram task pauses, so no log text can corrupt the binary stream.
// ============================================================================
#include "shot.h"
#include "config.h"
#include <Arduino.h>
#include <lvgl.h>

volatile bool gShotHold = false;

static volatile bool snapReq = false;
static uint32_t streamed = 0;

void shotRequest() {
  if (snapReq) return;
  lv_anim_del_all();                    // freeze every animation (world goes still)
  streamed = 0;
  snapReq = true;
  gShotHold = true;
  Serial.flush();
  Serial.updateBaudRate(921600);        // fast dump: 240*320*2 bytes in ~1.7 s
  lv_obj_invalidate(lv_scr_act());      // one full-screen redraw = the dump
}

void shotPoll() {
  if (snapReq || !Serial.available()) return;
  while (Serial.available()) {
    if (Serial.read() == 'S') { shotRequest(); break; }
  }
}

void shotFlushHook(int x1, int y1, int x2, int y2, const void* px, uint32_t bytes) {
  if (!snapReq) return;
  Serial.write((const uint8_t*)"BLK", 3);
  uint16_t hdr[4] = { (uint16_t)x1, (uint16_t)y1, (uint16_t)(x2 - x1 + 1), (uint16_t)(y2 - y1 + 1) };
  Serial.write((uint8_t*)hdr, sizeof hdr);
  Serial.write((const uint8_t*)px, bytes);
  streamed += bytes;
  if (streamed >= (uint32_t)SCREEN_W * SCREEN_H * 2) {   // whole frame is out
    Serial.print("\nOK\n");
    Serial.flush();
    Serial.updateBaudRate(115200);
    snapReq = false;
    gShotHold = false;
  }
}
