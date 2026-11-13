// ============================================================================
//  iPod player  -  ESP32-2432S028 (CYD)  -  see README.md
//  Board: ESP32 Dev Module   Core: esp32 by Espressif 2.0.17
//  Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
// ============================================================================
#include "config.h"
#include "settings.h"
#include "display.h"
#include "sdcard.h"
#include "buttons.h"
#include "library.h"
#include "audio.h"
#include "player.h"
#include "telegram.h"
#include "ui.h"
#include "hwtest.h"
#include "shot.h"

static void scanProgress(int done, int total, const char* name) {
  char b[64];
  if (total > 0) snprintf(b, sizeof b, "Loading music %d/%d\n%.28s", done + 1 > total ? total : done + 1, total, name);
  else snprintf(b, sizeof b, "Loading music...");
  uiSplash(b, total ? done * 100 / total : 100);
}

void setup() {
  Serial.begin(115200);
  settingsLoad();
  buttonsInit();
  delay(30);
  bool testMode = (digitalRead(PIN_BTN_PLAY) == LOW);

  displayInit();
  displaySetBrightness(gSettings.brightness);
  sdBegin();
  sdRegisterLvgl();
  libraryInit();
  audioInit();

  if (testMode) hwtestRun();

  uiInit();
#if INTRO_ENABLED
  if (!testMode) uiIntro();               // "Hello Sly" boot animation; any button skips it
#endif
  uiSplash(sdReady() ? "Loading music..." : "No SD card!", 0);
  if (sdReady()) libraryScan(scanProgress);
  playerInit();
  uiStart();
  if (!sdReady()) uiToast("SD card not found.\nUse FAT32 and a /music folder.");
  Serial.printf("Ready.songs=%d heap=%u\n", libraryCount(), (unsigned)ESP.getFreeHeap());
  // TODO: remove after WiFi debugging — auto-start the bot so no menu navigation is needed
  Serial.println("[tg] AUTOTEST: auto-starting WiFi/bot (no menu needed)");
  telegramStart();
}

void loop() {
  shotPoll();
  if (gShotHold) { displayLoop(); delay(2); return; }   // USB dump running: render only
  uiLoop();
  settingsLoop();
  delay(2);
}
