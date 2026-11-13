#pragma once
// ============================================================
//  THEME — accent colors + persisted settings (NVS)
//  Palette copied from the mock-up: near-black navy bg,
//  white text, thin light borders, one accent color.
//  Colours are written as 24-bit RGB and converted to RGB565.
// ============================================================
#include <Arduino.h>
#include <Preferences.h>

static inline uint16_t to565(uint32_t rgb)
{
  uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

struct ThemeColours { const char* name; uint16_t accent; };

// 5 presets required by the spec (green/blue/red/purple/orange)
static const ThemeColours THEMES[5] = {
  { "Green",  to565(0x1CE05E) },
  { "Blue",   to565(0x2E9BFF) },
  { "Red",    to565(0xFF4A57) },
  { "Purple", to565(0xA24BFF) },
  { "Orange", to565(0xFF9F2E) },
};

// base palette (from the mock-up)
static const uint16_t C_BG     = to565(0x0a0e14);  // near-black navy
static const uint16_t C_CARD   = to565(0x131a26);  // card fill (slightly lighter)
static const uint16_t C_BORDER = to565(0x3a4356);  // thin light grey-blue border
static const uint16_t C_TEXT   = to565(0xFFFFFF);  // white
static const uint16_t C_DIM    = to565(0x9aa4b8);  // muted grey for artist/dates
static const uint16_t C_TRACK  = to565(0x30363f);  // progress-bar track grey
static const uint16_t C_PANEL  = to565(0x0d1118);  // menu panel (darker/flatter)

static int    g_themeIdx   = 0;
static int    g_brightness = 160;   // 40..255 — floor 40 so it never goes fully black
static bool   g_shuffle    = false;
static int    g_volume     = 12;    // 1..20
static int    g_lastIdx    = 0;

static inline uint16_t accentCol() { return THEMES[g_themeIdx].accent; }

static void prefsLoad()
{
  Preferences p; p.begin("ipod", false);
  g_themeIdx    = p.getInt ("theme", 0);
  g_brightness  = p.getInt ("bright", 160);
  g_shuffle     = p.getBool("shuffle", false);
  g_volume      = p.getInt ("vol", 12);
  g_lastIdx     = p.getInt ("last", 0);
  p.end();
  if (g_themeIdx < 0 || g_themeIdx > 4)  g_themeIdx = 0;
  g_brightness = constrain(g_brightness, 40, 255);
  g_volume     = constrain(g_volume, 1, 20);
}

static void prefsSave()
{
  Preferences p; p.begin("ipod", false);
  p.putInt ("theme",   g_themeIdx);
  p.putInt ("bright",  g_brightness);
  p.putBool("shuffle", g_shuffle);
  p.putInt ("vol",     g_volume);
  p.putInt ("last",    g_lastIdx);
  p.end();
}
