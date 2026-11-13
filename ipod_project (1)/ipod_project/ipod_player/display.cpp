#include "display.h"
#include "config.h"
#include "settings.h"
#include "shot.h"
#include <lvgl.h>

LGFX tft;

LGFX::LGFX() {
  { auto c = bus.config();
    c.spi_host = HSPI_HOST;
    c.spi_mode = 0;
    c.freq_write = 40000000;
    c.freq_read  = 16000000;
    c.spi_3wire  = false;
    c.use_lock   = true;
    c.dma_channel = SPI_DMA_CH_AUTO;
    c.pin_sclk = PIN_TFT_SCLK;
    c.pin_mosi = PIN_TFT_MOSI;
    c.pin_miso = PIN_TFT_MISO;
    c.pin_dc   = PIN_TFT_DC;
    bus.config(c);
    panel.setBus(&bus); }
  { auto c = panel.config();
    c.pin_cs   = PIN_TFT_CS;
    c.pin_rst  = -1;
    c.pin_busy = -1;
    c.panel_width  = 240;
    c.panel_height = 320;
    c.offset_x = 0;
    c.offset_y = 0;
    c.offset_rotation = 0;
    c.readable   = false;
    c.invert     = PANEL_INVERT;
    c.rgb_order  = PANEL_BGR;
    c.dlen_16bit = false;
    c.bus_shared = false;
    panel.config(c); }
  { auto c = light.config();
    c.pin_bl      = PIN_TFT_BL;
    c.invert      = false;
    c.freq        = 12000;
    c.pwm_channel = 7;
    light.config(c);
    panel.setLight(&light); }
  setPanel(&panel);
}

// ------------------------------------------------------------------ LVGL ----
static lv_disp_draw_buf_t drawBuf;
static lv_disp_drv_t      dispDrv;
static lv_color_t*        buf1 = nullptr;
static lv_color_t*        buf2 = nullptr;
static const int          BUF_LINES = 20;
static bool               screenOn = true;

static void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  shotFlushHook(a->x1, a->y1, a->x2, a->y2, px, w * h * 2);
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixelsDMA((lgfx::swap565_t*)&px[0].full, w * h);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

void displaySetBrightness(uint8_t pct) {
  if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
  if (pct > 100) pct = 100;
  tft.setBrightness((uint32_t)pct * 255 / 100);
}

void displayInit() {
  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);
  tft.setBrightness(0);

  lv_init();
  buf1 = (lv_color_t*)heap_caps_malloc(SCREEN_W * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
  buf2 = (lv_color_t*)heap_caps_malloc(SCREEN_W * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, SCREEN_W * BUF_LINES);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res  = SCREEN_W;
  dispDrv.ver_res  = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);
}

void displayLoop() { lv_timer_handler(); }

void displayOff() { screenOn = false; tft.setBrightness(0); }
void displayOn()  { screenOn = true;  displaySetBrightness(gSettings.brightness); }
bool displayIsOn() { return screenOn; }
