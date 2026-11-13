#include "hwtest.h"
#include "config.h"
#include "display.h"
#include "buttons.h"
#include "audio.h"
#include "sdcard.h"

static void line(int row, uint16_t col, const char* fmt, ...) {
  char b[48]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  tft.setTextColor(col, TFT_BLACK);
  tft.setTextPadding(SCREEN_W);
  tft.drawString(b, 4, 4 + row * 20);
}

void hwtestRun() {
  tft.fillScreen(TFT_BLACK);
  displaySetBrightness(100);
  tft.setTextSize(1);
  tft.setFont(&fonts::Font2);
  bool sd = sdReady();
  uint8_t mode = 0, vol = 30;
  audioSetVolume(vol);
  const char* MN[] = { "off", "440 Hz tone", "DC low (0 V)", "DC mid (1.65 V)", "DC high (3.3 V)" };
  uint32_t last = 0;
  BtnId lastBtn = BTN_NONE;
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("== HARDWARE TEST ==", 4, 4);
  for (;;) {
    buttonsPoll();
    BtnEvent e;
    while (buttonsGet(e)) {
      if (e.type == BE_PRESS) {
        lastBtn = e.id;
        if (e.id == BTN_PLAY) { mode = (mode + 1) % 5; audioTestMode(mode); }
        if (e.id == BTN_VOLUP && vol < 100) { vol += 5; audioSetVolume(vol); }
        if (e.id == BTN_VOLDN && vol > 0)   { vol -= 5; audioSetVolume(vol); }
      }
      if (e.type == BE_RELEASE && e.id == lastBtn) lastBtn = BTN_NONE;
    }
    if (millis() - last > 120) {
      last = millis();
      uint32_t mv = buttonsReadLadderMv();
      line(1, TFT_WHITE, "IO35 ladder: %4u mV", (unsigned)mv);
      line(2, TFT_YELLOW, "Button: %s", buttonName(lastBtn));
      line(3, digitalRead(PIN_BTN_PLAY) ? TFT_DARKGREY : TFT_CYAN, "IO22 PLAY: %s", digitalRead(PIN_BTN_PLAY) ? "up" : "PRESSED");
      line(4, digitalRead(PIN_BTN_HOME) ? TFT_DARKGREY : TFT_CYAN, "IO27 HOME: %s", digitalRead(PIN_BTN_HOME) ? "up" : "PRESSED");
      line(5, sd ? TFT_GREEN : TFT_RED, "SD card: %s", sd ? "OK" : "NOT FOUND");
      if (sd) line(6, TFT_WHITE, "SD: %lu MB free", (unsigned long)(sdFreeBytes() / 1048576ULL));
      line(7, TFT_WHITE, "Free heap: %u", (unsigned)ESP.getFreeHeap());
      line(9, TFT_ORANGE, "Audio: %s", MN[mode]);
      line(10, TFT_WHITE, "Volume: %u%%", vol);
      line(12, TFT_DARKGREY, "PLAY = next audio test");
      line(13, TFT_DARKGREY, "VOL+ / VOL- = volume");
      line(14, TFT_DARKGREY, "Measure DC levels on the");
      line(15, TFT_DARKGREY, "R4 pad (before the cap).");
      line(16, TFT_DARKGREY, "Power off to leave.");
    }
    delay(2);
  }
}
