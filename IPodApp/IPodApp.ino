// ============================================================
//  IPodApp — DIY iPod-style MP3 player for ESP32-2432S028 (CYD)
//
//  Modules (include order matters — each header uses the ones above it):
//    secrets.h    WiFi + Telegram credentials (user-filled)
//    lgfx_cyd.h   ILI9341 display via LovyanGFX
//    theme.h      accent colours + NVS settings
//    player.h     SD playlist, ID3, covers, MP3 -> DAC GPIO26 (R4 wire)
//    telegram.h   WiFi + bot song installer (active only in WiFi menu)
//    ui.h         screens, button engine, sidebar
// ============================================================
#include <Arduino.h>
#include "secrets.h"
#include "lgfx_cyd.h"
#include "theme.h"
#include "player.h"
#include "telegram.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("[iPodApp] booting...");

  prefsLoad();                 // theme / brightness / shuffle / volume / last track
  uiInit();                    // display + button pins + splash

  playerInit();                // SD mount + scan + start audio task
  Serial.printf("[iPodApp] playlist: %d track(s)\n", g_trackCount);

  // Restore last-sung track, paused (press PLAY to start it).
  if (g_trackCount > 0) {
    int last = g_lastIdx;
    if (last < 0 || last >= g_trackCount) last = 0;
    playIdx(last, false);      // load paused -> audio task sets g_uiTrackDirty
    Serial.printf("[iPodApp] resuming #%d (paused)\n", last);
  }
  g_scrDirty = true;           // first full draw now has real data

  if (WIFI_CONFIGURED)
    Serial.println("[iPodApp] Telegram configured. Sidebar > WiFi to enable the bot.");
  else
    Serial.println("[iPodApp] Edit IPodApp/secrets.h to enable the Telegram bot.");

  Serial.println("[iPodApp] ready.");
  Serial.printf("[sys] free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
}

void loop()
{
  uiTick();      // input engine + all redraws + auto screen-off + BOOT key
  wifiStep();    // Telegram polling (no-ops unless WiFi menu is open)
  delay(2);      // keep the idle task fed
}
