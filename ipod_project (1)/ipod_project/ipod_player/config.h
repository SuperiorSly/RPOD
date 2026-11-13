// ============================================================================
//  config.h  -  every setting you might want to change lives here
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------- DISPLAY ---
// If the screen stays white / shows garbage -> change PANEL_ST7789 (0 <-> 1)
// If colours look inverted (black<->white)  -> change PANEL_INVERT (0 <-> 1)
// If red and blue are swapped               -> change PANEL_BGR    (0 <-> 1)
// (Flash display_test/display_test.ino first if you are not sure.)
#define PANEL_ST7789   1        // 0 = ILI9341, 1 = ST7789  <-- set to 1: this 2-USB CYD unit has the ST7789 panel
#define PANEL_INVERT   0
#define PANEL_BGR      0

#define SCREEN_W       240      // portrait
#define SCREEN_H       320

#define PIN_TFT_SCLK   14
#define PIN_TFT_MOSI   13
#define PIN_TFT_MISO   12
#define PIN_TFT_CS     15
#define PIN_TFT_DC      2
#define PIN_TFT_BL     21       // backlight (PWM)

// ------------------------------------------------------------------- SD -----
#define PIN_SD_SCK     18
#define PIN_SD_MISO    19
#define PIN_SD_MOSI    23
#define PIN_SD_CS       5

#define DIR_MUSIC      "/music"
#define DIR_DATA       "/ipod"
#define DIR_COVERS     "/ipod/covers"
#define DIR_THUMBS     "/ipod/thumbs"
#define FILE_INDEX     "/ipod/index.bin"
#define FILE_TMPJPG    "/ipod/tmp.jpg"

#define MAX_SONGS      120
#define COVER_PX       176      // big cover on the Now Playing screen
#define THUMB_PX        48      // little cover in the playlist rows

// ------------------------------------------------------------- BUTTONS ------
#define PIN_BTN_PLAY   22       // Play / Pause / Select   (direct, to GND)
#define PIN_BTN_HOME   27       // Home / menu             (direct, to GND)
#define PIN_LADDER     35       // 5 buttons on a resistor ladder (ADC1)

// Ladder voltages (millivolts) seen on IO35.  Upper limit of each button.
// Ideal values: Back 0, Vol+ 1100, Vol- 1650, Up 1980, Down 2200, idle 3300
// Use the hardware test (hold PLAY while powering on) to see your real values.
#define LADDER_MV_BACK_MAX    550
#define LADDER_MV_VOLUP_MAX  1375
#define LADDER_MV_VOLDN_MAX  1815
#define LADDER_MV_UP_MAX     2090
#define LADDER_MV_DOWN_MAX   2750     // anything above this = nothing pressed

#define BTN_DEBOUNCE_MS       18
#define BTN_REPEAT_DELAY_MS  450      // hold this long before auto-repeat starts
#define BTN_REPEAT_MS        120      // auto-repeat period
#define SEEK_STEP_MS        3000      // hold Up/Down on Now Playing = +-3 s
#define SEEK_REPEAT_MS       300      // how often a 3 s jump is done while held
#define TAP_GAP_MS           550      // max gap between the 3 taps for next/prev
#define VOLUME_STEP            5      // percent per press

// -------------------------------------------------------------- SCREEN ------
#define SCREEN_OFF_MS       20000     // backlight off after this idle time
#define SCREEN_ANIM_MS        220     // Now Playing <-> Playlist slide (0 = instant)
#define MENU_ANIM_MS          240     // sidebar slide
#define BRIGHT_MIN_PCT         10     // brightness never goes below this
#define BRIGHT_STEP_PCT        10

// --------------------------------------------------------------- AUDIO ------
#define PIN_DAC_AUDIO         26      // ESP32 DAC2 -> earphone jack (R4 tap)
#define AUDIO_DEFAULT_VOLUME  60      // percent, first boot
#define AUDIO_MAX_GAIN_PCT   100      // 100 = full scale. Lower it if too loud.
#define AUDIO_DITHER           0      // 1 = add a little noise (can mask 8-bit grain)

// ------------------------------------------------------------ TELEGRAM ------
#define TG_MAX_FILE_BYTES   (20UL * 1024UL * 1024UL)  // Bot API download limit
#define TG_POLL_TIMEOUT_S      2

// --- Bot-over-PC relay -------------------------------------------------------
// WiFi + TLS cannot coexist on this board's RAM: once the radio stack is up the
// biggest free heap block (~24-31 KB) is too small for the TLS handshake's
// ~40-46 KB, and every request dies with "SSL - Memory allocation failed".
// (Reserving a big block first starves WiFi itself instead.)
// So the bot now talks PLAIN LAN HTTP to a tiny relay script that runs on the
// PC ("tg_relay.ps1" in this project root) and does the HTTPS leg itself.
// 192.168.1.101 = the PC's LAN address. If the PC's IP ever changes, update it
// here; the relay answers nothing unless the PC is powered and the script runs.
#define TG_RELAY_HOST        "192.168.1.101:8461"

// ------------------------------------------------------- MORE DISPLAY ------
// Screen upside-down?  Change SCREEN_ROTATION (0, 1, 2 or 3).  0/2 = portrait.
#define SCREEN_ROTATION        0

// ------------------------------------------------------------ BOOT INTRO ----
// Tiny "Hello <NAME>" LVGL animation at power-up (~INTRO_MS, skipped by any
// button button-press). Labels + 6 dots only - no images, no RAM cost.
// 0 = disabled (boot exactly like before).
#define INTRO_ENABLED  1
#define INTRO_NAME     "Sly"
#define INTRO_MS       3000     // total length, skipped by any button
