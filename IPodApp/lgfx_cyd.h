#pragma once
// ============================================================
//  LGFX_CYD — display driver config for ESP32-2432S028 ("CYD")
//  Panel: on HSPI (SCK14, MOSI13, MISO12, CS15, DC2)
//  Backlight: GPIO21 (PWM).
//  Touch: XPT2046 shares the TFT SPI bus; its CS is GPIO33, IRQ GPIO36.
//  Safe with the DAC audio (GPIO25/26 are separate pins; the bus lock
//  serialises touch reads against screen drawing).
//
//  PANEL SWAP — the dual-USB CYD ships different LCD controllers
//  depending on factory batch, all on identical yellow PCBs:
//    0 = lgfx::Panel_ILI9341  (classic single-USB config)
//    1 = lgfx::Panel_ST7789   (most 2-USB units)
//    2 = lgfx::Panel_ILI9341 with reversed R/B wiring clone fix
//  If the screen is still "weird" after a reflash, JUST change
//  PANEL_VARIANT below and reflash — no other edits needed.
// ============================================================
#include <LovyanGFX.hpp>

#ifndef PANEL_VARIANT
#define PANEL_VARIANT 1        // <<<< TRY 1 (ST7789) first on this 2-USB unit
#endif

class LGFX_CYD : public lgfx::LGFX_Device {
#if PANEL_VARIANT == 1
  lgfx::Panel_ST7789   _panel;
#else
  lgfx::Panel_ILI9341  _panel;
#endif
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX_CYD(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;   // HSPI = the CYD TFT bus
      cfg.spi_mode    = 0;
      cfg.freq_write  = 27000000;    // 27 MHz (40 glitched on this panel clone)
      cfg.freq_read   = 8000000;
      cfg.spi_3wire   = false;       // CYD has a REAL DC wire (GPIO2) -> not 3-wire mode
      cfg.pin_dc      = 2;           // ILI9341 data/command pin on the CYD
      cfg.use_lock    = true;
      cfg.pin_sclk    = 14;
      cfg.pin_mosi    = 13;
      cfg.pin_miso    = 12;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs           = 15;
      cfg.pin_rst          = -1;
      cfg.pin_busy         = -1;
      cfg.panel_width      = 240;
      cfg.panel_height     = 320;
      cfg.memory_width     = 240;
      cfg.memory_height    = 320;
      cfg.offset_rotation  = 0;
#if   PANEL_VARIANT == 1                              // ST7789 2-USB (community reference config)
      cfg.invert           = false;                   // CYD 2-USB units: NOT inverted
      cfg.rgb_order        = false;
#elif PANEL_VARIANT == 2                              // ILI9341 dual-USB clone, R/B reversed
      cfg.invert           = false;
      cfg.rgb_order        = true;                    // fixes red<->blue swap on dual-USB clones
#else                                                 // classic single-USB ILI9341
      cfg.invert           = false;
      cfg.rgb_order        = false;
#endif
      _panel.config(cfg);
    }
    {
      auto cfg = _touch.config();
      cfg.bus_shared   = true;   // XPT2046 hangs off the same SPI bus as the panel
      cfg.pin_cs       = 33;     // CYD touch chip-select
      cfg.pin_int      = 36;     // CYD touch IRQ
      cfg.x_min        = 300;    // CYD community calibration
      cfg.x_max        = 3900;
      cfg.y_min        = 3700;   // note: Y runs inverted on this panel
      cfg.y_max        = 200;
      cfg.freq         = 1000000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl       = 21;
      cfg.freq         = 12000;
      cfg.pwm_channel  = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX_CYD tft;

// Portrait (rotation 0 = native 240x320 for this panel).
// If the image shows upside-down after a reflash, change to 2.
// If sideways, it would need an odd number - the UI is portrait-coded.
#define UI_ROTATION 0
