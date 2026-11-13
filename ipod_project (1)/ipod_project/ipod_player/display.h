#pragma once
#include <Arduino.h>
#define LGFX_USE_V1
#include "config.h"
#define PANEL_ST7789_SEL PANEL_ST7789
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_SPI      bus;
  lgfx::Light_PWM    light;
#if PANEL_ST7789_SEL
  lgfx::Panel_ST7789  panel;
#else
  lgfx::Panel_ILI9341 panel;
#endif
  LGFX();
};

extern LGFX tft;

void displayInit();              // panel + LVGL + backlight
void displayLoop();              // lv_timer_handler wrapper
void displaySetBrightness(uint8_t pct);   // 0..100 (clamped to BRIGHT_MIN_PCT)
void displayOff();               // backlight off (music keeps playing)
void displayOn();
bool displayIsOn();
