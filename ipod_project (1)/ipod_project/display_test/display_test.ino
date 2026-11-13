// Display check for the ESP32-2432S028 (CYD).  Flash this FIRST if the main
// firmware shows a white screen, wrong colours or negative colours.
// It cycles through 4 settings every 5 seconds and writes the settings on screen.
// Note which one shows: RED, GREEN, BLUE bars in that order, white text on black.
// Then put the same values into ipod_player/config.h (PANEL_ST7789 / PANEL_INVERT).
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
  lgfx::Panel_ILI9341 pA;
  lgfx::Panel_ST7789  pB;
  LGFX() {
    auto c = bus.config();
    c.spi_host = HSPI_HOST; c.spi_mode = 0; c.freq_write = 40000000; c.freq_read = 16000000;
    c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
    c.pin_sclk = 14; c.pin_mosi = 13; c.pin_miso = 12; c.pin_dc = 2;
    bus.config(c);
    auto lc = light.config(); lc.pin_bl = 21; lc.freq = 12000; lc.pwm_channel = 7; light.config(lc);
    setupPanel(pA); setupPanel(pB);
    pA.setBus(&bus); pB.setBus(&bus);
    pA.setLight(&light); pB.setLight(&light);
    setPanel(&pA);
  }
  template <class P> void setupPanel(P& p) {
    auto c = p.config();
    c.pin_cs = 15; c.pin_rst = -1; c.panel_width = 240; c.panel_height = 320;
    c.readable = false; c.invert = false; c.rgb_order = false; c.bus_shared = false;
    p.config(c);
  }
  void useST7789(bool v) { setPanel(v ? (lgfx::Panel_Device*)&pB : (lgfx::Panel_Device*)&pA); }
};

static LGFX tft;
static int step = 0;

void show(bool st, bool inv) {
  tft.useST7789(st);
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(200);
  tft.invertDisplay(inv);
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 100, 80, 120, TFT_RED);
  tft.fillRect(80, 100, 80, 120, TFT_GREEN);
  tft.fillRect(160, 100, 80, 120, TFT_BLUE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setFont(&fonts::Font4);
  tft.drawString(st ? "ST7789" : "ILI9341", 10, 10);
  tft.drawString(inv ? "INVERT = 1" : "INVERT = 0", 10, 45);
  tft.setFont(&fonts::Font2);
  tft.drawString("Want: RED GREEN BLUE", 10, 240);
  tft.drawString("white text on black", 10, 262);
}

void setup() {}

void loop() {
  bool st = (step & 2), inv = (step & 1);
  show(st, inv);
  step = (step + 1) & 3;
  delay(5000);
}
