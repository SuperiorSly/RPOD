#pragma once
// ============================================================
//  UI — Now Playing / Playlist("Start of Story") / Menu sidebar
//  + button input engine (debounce, hold+repeat, triple press)
//  Layout matches the SquareLine mock-up (portrait 240x320).
// ============================================================
#include <Arduino.h>
#include <esp_sleep.h>
#include "lgfx_cyd.h"

// ---------------- screens / menu states ----------------
enum Screen    { SCR_NOW, SCR_LIST };
enum MenuState { MENU_CLOSED, MENU_LIST, MENU_THEMES, MENU_BRIGHT, MENU_WIFI };

static Screen    g_scr        = SCR_NOW;
static MenuState g_menu       = MENU_CLOSED;
static int       g_listCur    = 0, g_listScroll = 0;
static int       g_menuCur    = 0;
static int       g_themeCur   = 0;      // selection while browsing themes
static int       g_brightStep = 160;
static bool      g_screenOn   = true;
static uint32_t  g_lastInput  = 0;
static bool      g_scrDirty   = true;   // full redraw next loop

static const uint32_t SCREEN_OFF_MS = 20000;      // brief §4.4
static const int  MENU_W = 142;                   // sidebar width (58% of 240)
static const int  MENU_X  = 240 - MENU_W;        // 98

// ---------------- fonts ----------------
static const lgfx::IFont* F_S9    = &fonts::FreeSans9pt7b;     // small text
static const lgfx::IFont* F_12    = &fonts::FreeSans12pt7b;    // regular
static const lgfx::IFont* F_B12   = &fonts::FreeSansBold12pt7b;// bold titles

// ================= input engine (Phase 2 pinout) =================
static const uint8_t PIN_PLAY = 22, PIN_HOME = 27, PIN_LADDER = 35;

static const int TH_BACK_HI  = 250;
static const int TH_VOLP_LO =  700,  TH_VOLP_HI  = 1700;
static const int TH_VOLM_LO = 1800,  TH_VOLM_HI  = 2250;
static const int TH_UP_LO   = 2350,  TH_UP_HI    = 2595;
static const int TH_DOWN_LO = 2625,  TH_DOWN_HI  = 2950;
static const int TH_IDLE    = 3500;

enum BtnId : uint8_t { BID_NONE, BID_PLAY, BID_HOME, BID_BACK, BID_VOLP, BID_VOLM, BID_UP, BID_DOWN, BID_UNK };
static const char* const BTN_NAME2[] = { "none","PLAY","HOME","BACK","VOL+","VOL-","UP","DOWN","??" };

const uint16_t DEBOUNCE_MS   = 30;
const uint16_t HOLD_MS       = 500;
const uint16_t REPEAT_MS     = 300;      // volume repeat / seek steps
const uint16_t TAP_MAX_MS    = 400;
const uint16_t TAP_WINDOW_MS = 450;

struct InEvt  { uint8_t kind; uint8_t btn; };  // kind: 0 short,1 double,2 triple,3 holdStart,4 holdRepeat
static InEvt  g_q[10]; static uint8_t g_qHead = 0, g_qTail = 0;

enum LadderName { L_NONE=0, L_BACK, L_VOLP, L_VOLM, L_UP, L_DOWN, L_UNK };

struct BtnRun {
  bool     lastRaw, committed;
  uint32_t changedAt, pressAt, nextRepeat;
  uint16_t repeatCount;
  bool     holdAnnounced;
  uint8_t  taps;
  bool     tapPending;
  uint32_t tapDeadline;
};
static BtnRun s_run[8];

static LadderName decodeLadder(int a)
{
  if (a >= TH_IDLE)  return L_NONE;
  if (a <  TH_BACK_HI) return L_BACK;
  if (a >= TH_VOLP_LO && a < TH_VOLP_HI) return L_VOLP;
  if (a >= TH_VOLM_LO && a < TH_VOLM_HI) return L_VOLM;
  if (a >= TH_UP_LO   && a < TH_UP_HI)   return L_UP;
  if (a >= TH_DOWN_LO && a < TH_DOWN_HI) return L_DOWN;
  return L_UNK;
}

static bool rawPressed(BtnId id, LadderName l)
{
  switch (id) {
    case BID_PLAY: return digitalRead(PIN_PLAY) == LOW;
    case BID_HOME: return digitalRead(PIN_HOME) == LOW;
    case BID_BACK: return l == L_BACK;
    case BID_VOLP: return l == L_VOLP;
    case BID_VOLM: return l == L_VOLM;
    case BID_UP:   return l == L_UP;
    case BID_DOWN: return l == L_DOWN;
    default:       return false;
  }
}

static void emit(uint8_t kind, uint8_t btn)
{
  g_q[g_qHead] = { kind, btn };
  g_qHead = (g_qHead + 1) % 10;
}

static void commitBtn(BtnRun& b, BtnId id, bool pressed, LadderName l)
{
  b.committed = pressed;
  if (pressed) {
    b.pressAt = millis(); b.holdAnnounced = false; b.repeatCount = 0; b.nextRepeat = 0;
  } else {
    uint32_t dur = millis() - b.pressAt;
    if (!b.holdAnnounced && dur <= TAP_MAX_MS) {
      b.taps++;
      if (b.taps >= 3) { emit(2, id); b.taps = 0; b.tapPending = false; }
      else { b.tapPending = true; b.tapDeadline = millis() + TAP_WINDOW_MS; }
    } else {
      b.taps = 0; b.tapPending = false;
    }
  }
}

static void inputTick()
{
  LadderName l = decodeLadder(analogRead(PIN_LADDER));
  uint32_t now = millis();

  for (uint8_t id = 1; id <= 7; id++) {
    BtnRun& b = s_run[id];
    bool raw = rawPressed((BtnId)id, l);
    if (raw != b.lastRaw) { b.lastRaw = raw; b.changedAt = now; }
    if (raw != b.committed && (now - b.changedAt) >= DEBOUNCE_MS) commitBtn(b, (BtnId)id, raw, l);

    if (b.committed) {
      if (!b.holdAnnounced && (now - b.pressAt) >= HOLD_MS) {
        b.holdAnnounced = true; b.repeatCount = 0; b.nextRepeat = now + REPEAT_MS;
        emit(3, id);
      }
      if (b.holdAnnounced && (int32_t)(now - b.nextRepeat) >= 0) {
        b.repeatCount++; b.nextRepeat += REPEAT_MS;
        emit(4, id);
      }
    }
    if (b.tapPending && (int32_t)(now - b.tapDeadline) >= 0) {
      emit(b.taps == 1 ? 0 : 1, id);
      b.taps = 0; b.tapPending = false;
    }
  }
}

// ================= drawing helpers =================
static void fmtTime(uint32_t ms, char* out)
{
  uint32_t s = ms / 1000;
  snprintf(out, 8, "%u:%02u", s / 60, s % 60);
}

// draw `s` left-aligned at x,y; if it is wider than maxW, chop characters
// from the end and add "..." until it fits.
static void drawTrunc(int x, int y, const char* s, int maxW, uint16_t col,
                      const lgfx::IFont* f, uint16_t bg = C_BG)
{
  tft.setFont(f);
  if ((int)tft.textWidth(s) <= maxW) {
    tft.setTextColor(col, bg);
    tft.drawString(s, x, y);
    return;
  }
  char buf[64];
  strncpy(buf, s, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
  while (buf[0] && buf[1]) {
    char probe[68];
    snprintf(probe, sizeof(probe), "%s...", buf);
    if ((int)tft.textWidth(probe) <= maxW) break;
    buf[strlen(buf) - 1] = 0;                       // drop last char, retry
  }
  char out[68];
  snprintf(out, sizeof(out), "%s...", buf);
  tft.setTextColor(col, bg);
  tft.drawString(out, x, y);
}

static void drawProgress(int x, int y, int w, int h, float p, uint16_t acc)
{
  tft.fillRoundRect(x, y, w, h, h / 2, C_TRACK);
  int fw = (int)(w * p);
  if (fw < h) fw = h;                       // keep pill shape positive
  if (fw > w) fw = w;
  if (p > 0.02f) tft.fillRoundRect(x, y, max(h, fw), h, h / 2, C_TEXT);
  (void)acc;
}

// fallback cover: rounded card + simple note symbol
static void drawDefaultCover(int x, int y, int s)
{
  tft.fillRoundRect(x, y, s, s, 12, C_CARD);
  tft.drawRoundRect(x, y, s, s, 12, accentCol());
  int cx = x + s / 2, cy = y + s / 2, r = s / 4;
  tft.fillEllipse(cx - r / 3, cy + r / 2, r / 2, r / 3, accentCol());
  tft.fillRect(cx + r / 6, cy - r, max(2, s / 48), r + r / 2, accentCol());
}

static void drawCover(const Track& t, int x, int y, int s, bool big)
{
  char cpath[64]; coverCacheName(t, big, cpath, sizeof(cpath));
  if (drawCachedCover(cpath, x, y, s, s)) return;
  if (decodeCoverJpeg(const_cast<Track&>(t), big)) {
    drawCachedCover(cpath, x, y, s, s);
  } else {
    drawDefaultCover(x, y, s);
  }
}

// ================= Screen 1: NOW PLAYING =================
static void drawNowDynamic();
static void drawNowStatic()
{
  tft.fillScreen(C_BG);
  tft.setFont(F_B12);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Now Playing", 120, 16);
  tft.setTextDatum(TL_DATUM);

  if (g_curTrack >= 0) drawCover(g_tracks[g_curTrack], 40, 34, CVR_W, true);
  drawNowDynamic();
}

static void drawNowDynamic()
{
  int X = 40, W = CVR_W, Y = 34;
  int ty = Y + CVR_W + 12;                     // 206
  if (g_curTrack < 0) {
    drawTrunc(X, ty, "No songs", W, C_DIM, F_B12);
    drawTrunc(X, ty + 22, "Insert SD card", W, C_DIM, F_S9);
    return;
  }
  Track& t = g_tracks[g_curTrack];
  drawTrunc(X, ty, t.title, W, C_TEXT, F_B12);
  drawTrunc(X, ty + 22, t.artist, W, C_DIM, F_S9);

  PlayerState ps; playerSnapshot(ps);
  Track& ct = g_tracks[g_curTrack];
  float p = ct.durMs ? (float)ps.elapsedMs / ct.durMs : 0;
  if (p > 1) p = 1;
  drawProgress(X, 250, W, 6, p, accentCol());

  char a[8], b[8];
  fmtTime(ps.elapsedMs, a); fmtTime(ct.durMs, b);
  tft.setFont(F_S9);
  tft.setTextColor(C_TEXT, C_BG);
  tft.drawString(a, X, 262);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(b, X + W, 262);
  tft.setTextDatum(TL_DATUM);
}

// ================= Screen 2: PLAYLIST ("Start of Story") =================
static const int ROW_H = 84, ROW_Y0 = 40, ROW_X = 14, ROW_W = 212;

static void drawListRow(int rowY, int idx, bool selected)
{
  Track& t = g_tracks[idx];
  uint16_t border = selected ? accentCol() : C_BORDER;
  tft.fillRoundRect(ROW_X, rowY, ROW_W, ROW_H, 10, C_CARD);
  tft.drawRoundRect(ROW_X, rowY, ROW_W, ROW_H, 10, border);
  if (selected) tft.drawRoundRect(ROW_X + 1, rowY + 1, ROW_W - 2, ROW_H - 2, 10, border);

  static char thp[64]; coverCacheName(t, false, thp, sizeof(thp));
  if (!drawCachedCover(thp, ROW_X + 12, rowY + 12, TH_W, TH_W)) {
    if (decodeCoverJpeg(t, false)) {
      coverCacheName(t, false, thp, sizeof(thp));
      drawCachedCover(thp, ROW_X + 12, rowY + 12, TH_W, TH_W);
    } else {
      drawDefaultCover(ROW_X + 12, rowY + 12, TH_W);
    }
  }

  int tx = ROW_X + 12 + TH_W + 12;
  uint16_t titleCol = (idx == g_curTrack) ? accentCol() : C_TEXT;
  drawTrunc(tx, rowY + 14, t.title, ROW_W - (tx - ROW_X) - 52, titleCol, F_B12, C_CARD);
  drawTrunc(tx, rowY + 40, t.artist, ROW_W - (tx - ROW_X) - 52, C_DIM, F_S9, C_CARD);

  char d[8]; fmtTime(t.durMs, d);
  tft.setFont(F_S9);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(d, ROW_X + ROW_W - 12, rowY + ROW_H / 2);
  tft.setTextDatum(TL_DATUM);
}

static void drawListScreen()
{
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 240, 30, C_CARD);
  tft.drawFastHLine(0, 30, 240, C_BORDER);
  tft.setFont(F_12);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Start of Story", 120, 15);   // header per user decision
  tft.setTextDatum(TL_DATUM);

  if (g_trackCount == 0) {
    tft.setFont(F_S9);
    tft.setTextColor(C_DIM, C_BG);
    tft.setTextDatum(CC_DATUM);
    tft.drawString("No songs on SD card", 120, 170);
    tft.drawString("(Insert card + add via WiFi)", 120, 190);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  for (int k = 0; k < 3; k++) {
    int idx = g_listScroll + k;
    if (idx >= g_trackCount) break;
    drawListRow(ROW_Y0 + k * (ROW_H + 8), idx, idx == g_listCur);
  }
}

// ================= Screen 3: MENU sidebar =================
static const char* const MENU_ITEMS[5] = { "WiFi", "Bluetooth", "Shuffle Playlist", "Brightness", "Settings" };

static void drawMenuItems(int x, int sel)
{
  int iy = 76;
  tft.setFont(F_S9);
  for (int i = 0; i < 5; i++) {
    tft.fillRoundRect(x + 8, iy, MENU_W - 16, 30, 8, C_CARD);
    tft.drawRoundRect(x + 8, iy, MENU_W - 16, 30, 8, i == sel ? accentCol() : C_BORDER);
    tft.setTextColor(C_TEXT, C_CARD);
    drawTrunc(x + 14, iy + 8, MENU_ITEMS[i], MENU_W - 30, C_TEXT, F_S9, C_CARD);
    if (i == 2) {                               // shuffle On/Off on the right
      char sh[6]; strcpy(sh, g_shuffle ? "On" : "Off");
      tft.setTextColor(C_DIM, C_CARD);
      tft.setTextDatum(CR_DATUM);
      tft.drawString(sh, x + MENU_W - 14, iy + 15);
      tft.setTextDatum(TL_DATUM);
    }
    iy += 38;
  }
}

static void drawWifiPane(int x)
{
  tft.setFont(F_S9);
  tft.setTextColor(C_TEXT, C_PANEL);
  const char* line;
  switch (g_wifiStage) {
    case W_CONNECT:  line = "Connecting WiFi...";  break;
    case W_WAIT:     line = strlen(g_wifiMsg) ? g_wifiMsg : "Send me a song!"; break;
    case W_PROGRESS: line = g_wifiMsg;             break;
    case W_DONE:     line = "Done!";               break;
    case W_ERR:      line = strlen(g_wifiMsg) ? g_wifiMsg : "Error"; break;
    default:         line = "WiFi off";            break;
  }
  tft.drawString(line, x + 10, 300);
  if (g_wifiStage == W_PROGRESS) {
    float p = g_wifiPct < 0 ? 0.05f : (float)g_wifiPct / 100.0f;
    drawProgress(x + 10, 310, MENU_W - 20, 6, p, accentCol());
  }
}

// draws panel at strip starting x (for the slide animation)
static void drawMenuPanel(int x)
{
  tft.fillRect(x, 0, 240 - x, 320, C_PANEL);
  tft.drawFastVLine(x, 0, 320, C_BORDER);
  tft.setFont(F_B12);
  tft.setTextColor(C_TEXT, C_PANEL);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Menu", x + MENU_W / 2 - 10, 54);
  tft.setTextDatum(TL_DATUM);

  if (g_menu == MENU_LIST)  drawMenuItems(x, g_menuCur);
  if (g_menu == MENU_THEMES) {
    int iy = 66;
    for (int i = 0; i < 5; i++) {
      tft.fillRoundRect(x + 8, iy, MENU_W - 16, 30, 8, C_CARD);
      tft.drawRoundRect(x + 8, iy, MENU_W - 16, 30, 8, i == g_themeCur ? accentCol() : C_BORDER);
      tft.fillCircle(x + 22, iy + 15, 7, THEMES[i].accent);
      tft.setTextColor(C_TEXT, C_CARD);
      tft.drawString(THEMES[i].name, x + 36, iy + 8);
      if (i == g_themeIdx) {                      // saved theme marker
        tft.setTextColor(accentCol(), C_CARD);
        tft.drawString("*", x + MENU_W - 20, iy + 8);
      }
      iy += 36;
    }
  }
  if (g_menu == MENU_BRIGHT) {
    tft.fillRoundRect(x + 8, 66, MENU_W - 16, 60, 8, C_CARD);
    tft.drawRoundRect(x + 8, 66, MENU_W - 16, 60, 8, accentCol());
    tft.setFont(F_S9);
    tft.setTextColor(C_TEXT, C_CARD);
    char pc[10]; snprintf(pc, sizeof(pc), "%d%%", g_brightStep * 100 / 255);
    tft.drawString(pc, x + 16, 72);
    int bx = x + 16, bw = MENU_W - 32, by = 92;
    int fw = (int)(bw * g_brightStep / 255);
    tft.fillRoundRect(bx, by, fw, 6, 3, C_TEXT);
    tft.drawRoundRect(bx, by, bw, 6, 3, C_TRACK);
  }
  if (g_menu == MENU_WIFI) {
    drawMenuItems(x, g_menuCur);
    drawWifiPane(x);
  }
}

static void animateMenu(bool in)
{
  // slide ~200 ms, 8 steps; underlying playlist strip redrawn each step
  uint32_t t = millis();
  for (int step = 0; step <= 8; step++) {
    float e = (float)step / 8.0f;                                  // ease-out
    e = 1 - ((1 - e) * (1 - e));
    int x;
    if (in)  x = 240 - (int)((240 - MENU_X) * e);      // 240 -> MENU_X (slide in)
    else     x = MENU_X + (int)((240 - MENU_X) * e);   // MENU_X -> 240 (slide out)
    if (x > 240) x = 240;
    tft.fillRect(max(x - 2, MENU_X), 0, 240 - max(x - 2, MENU_X), 320, C_BG);
    drawListScreen();                                              // restores left part
    drawMenuPanel(x);
    while (millis() - t < (uint32_t)(step + 1) * 24) vTaskDelay(1);
  }
  if (in) drawMenuPanel(MENU_X);
}

static void openMenu()
{
  if (g_scr == SCR_NOW) { g_scr = SCR_LIST; drawListScreen(); }
  g_menu = MENU_LIST;
  animateMenu(true);
  g_scrDirty = false;
}

static void closeMenu()
{
  animateMenu(false);
  g_menu = MENU_CLOSED;
  drawListScreen();
}

// toast = small bottom overlay
static char     g_toast[40] = "";
static uint32_t g_toastEnd = 0;
static void showToast(const char* m)
{
  strncpy(g_toast, m, sizeof(g_toast) - 1); g_toast[sizeof(g_toast) - 1] = 0;
  g_toastEnd = millis() + 1400;
  tft.fillRect(0, 300, 240, 20, C_CARD);
  tft.setFont(F_S9);
  tft.setTextColor(accentCol(), C_CARD);
  tft.setTextDatum(CC_DATUM);
  tft.drawString(g_toast, 120, 310);
  tft.setTextDatum(TL_DATUM);
}

// ================= event dispatch (brief §5) =================
static void volumeStep(int d)
{
  g_volume = constrain(g_volume + d, 1, 20);
  prefsSave();
  static uint32_t lv = 0;
  if (millis() - lv > 1500) {
    lv = millis();
    char msg[24];
    snprintf(msg, sizeof(msg), "Volume: %d/20", g_volume);
    showToast(msg);
  }
}

static void listEnsureVisible()
{
  if (g_listCur < g_listScroll) g_listScroll = g_listCur;
  if (g_listCur > g_listScroll + 2) g_listScroll = g_listCur - 2;
  if (g_listScroll < 0) g_listScroll = 0;
}

static void menuEvent(uint8_t b, uint8_t k)
{
  (void)k;
  if (b == BID_HOME) {
    if (g_menu == MENU_WIFI) wifiStop();        // leaving WiFi via HOME too
    closeMenu(); return;
  }

  if (b == BID_BACK) {
    if (g_menu == MENU_LIST) { closeMenu(); return; }
    wifiStop();
    g_menu = MENU_LIST; g_menuCur = 0;
    drawMenuPanel(MENU_X);
    return;
  }

  if (g_menu == MENU_BRIGHT) {
    if (b == BID_UP || b == BID_DOWN) {
      g_brightStep = constrain(g_brightStep + (b == BID_UP ? 15 : -15), 40, 255);
      tft.setBrightness(g_brightStep);                 // live change (spec)
      drawMenuPanel(MENU_X);
    }
    if (b == BID_PLAY) { g_brightness = g_brightStep; prefsSave(); g_menu = MENU_LIST; drawMenuPanel(MENU_X); }
    return;
  }

  if (g_menu == MENU_THEMES) {
    if (b == BID_UP)   { g_themeCur = (g_themeCur + 4) % 5; }
    if (b == BID_DOWN) { g_themeCur = (g_themeCur + 1) % 5; }
    drawMenuPanel(MENU_X);
    if (b == BID_PLAY) {
      g_themeIdx = g_themeCur; prefsSave();
      g_menu = MENU_LIST; g_scrDirty = true;           // recolor everything
    }
    return;
  }

  // MENU_LIST / MENU_WIFI
  if (b == BID_UP)   g_menuCur = (g_menuCur + 4) % 5;
  if (b == BID_DOWN) g_menuCur = (g_menuCur + 1) % 5;
  if (g_menu == MENU_LIST && (b == BID_UP || b == BID_DOWN)) drawMenuPanel(MENU_X);

  if (b == BID_PLAY && g_menu == MENU_LIST) {
    switch (g_menuCur) {
      case 0:  wifiStart(); g_menu = MENU_WIFI; drawMenuPanel(MENU_X); break;
      case 1:  showToast("Bluetooth: comes in phase 8"); break;
      case 2:  g_shuffle = !g_shuffle; prefsSave(); drawMenuPanel(MENU_X); break;
      case 3:  g_brightStep = g_brightness; g_menu = MENU_BRIGHT; drawMenuPanel(MENU_X); break;
      case 4:  g_themeCur = g_themeIdx; g_menu = MENU_THEMES; drawMenuPanel(MENU_X); break;
    }
    return;
  }
  if (b == BID_PLAY && g_menu == MENU_WIFI) {          // leaving WiFi
    wifiStop();
    g_menu = MENU_LIST;
    drawMenuPanel(MENU_X);
  }
}

static void handleEvent(uint8_t b, uint8_t k)
{
  if (b == BID_NONE || b == BID_UNK) return;

  if (!g_screenOn) {                                   // first press only wakes
    tft.setBrightness(g_brightness);
    g_screenOn = true; g_lastInput = millis();
    Serial.println("[ui] wake");
    return;
  }
  g_lastInput = millis();

  // volume keys work everywhere, including the sidebar
  if (b == BID_VOLP || b == BID_VOLM) {
    if (k == 0 || k == 4) volumeStep(b == BID_VOLP ? +1 : -1);
    return;
  }

  if (g_menu != MENU_CLOSED) { menuEvent(b, k); return; }

  if (g_scr == SCR_NOW) {
    switch (b) {
      case BID_PLAY: if (k == 0) playerToggle(); break;
      case BID_HOME: if (k == 0) openMenu(); break;
      case BID_BACK:
        if (k == 0) { g_scr = SCR_LIST; g_scrDirty = true; }
        break;
      case BID_UP:
        if (k == 2) playerNext();
        if (k == 4) playerSeekBy(+3000);
        break;
      case BID_DOWN:
        if (k == 2) playerPrev();
        if (k == 4) playerSeekBy(-3000);
        break;
      default: break;
    }
    return;
  }

  // SCR_LIST
  switch (b) {
    case BID_HOME: if (k == 0) openMenu(); break;
    case BID_BACK: if (k == 0) { g_scr = SCR_NOW; g_scrDirty = true; } break;
    case BID_UP:   if (k == 0 && g_trackCount) { g_listCur = (g_listCur + g_trackCount - 1) % g_trackCount; listEnsureVisible(); g_scrDirty = true; } break;
    case BID_DOWN: if (k == 0 && g_trackCount) { g_listCur = (g_listCur + 1) % g_trackCount; listEnsureVisible(); g_scrDirty = true; } break;
    case BID_PLAY:
      if (k == 0 && g_trackCount) {
        if (g_listCur == g_curTrack && g_playing) playerToggle();
        else playIdx(g_listCur, true);
      }
      break;
    default: break;
  }
}

// ================= touch input (XPT2046, taps only) =================
static void touchTap(int x, int y)
{
  if (g_menu != MENU_CLOSED) {                         // ---- sidebar open ----
    if (x < MENU_X) { menuEvent(BID_HOME, 0); return; }      // tap left of panel = close
    if (g_menu == MENU_LIST || g_menu == MENU_WIFI) {
      if (y >= 76 && y < 258) {                        // 5 rows: y 76..258, step 38
        g_menuCur = (y - 76) / 38;
        menuEvent(BID_PLAY, 0);                        // activate tapped row
      }
      return;
    }
    if (g_menu == MENU_THEMES) {
      if (y >= 66 && y < 240) {                        // 5 rows: y 66..240, step 36
        g_themeCur = (y - 66) / 36;
        menuEvent(BID_PLAY, 0);                        // pick tapped theme
      }
      return;
    }
    if (g_menu == MENU_BRIGHT) {                       // tap on the slider sets level
      if (y >= 66 && y < 126) {
        int v = (x - (MENU_X + 16)) * 255 / (MENU_W - 32);
        g_brightStep = constrain(v, 40, 255);
        tft.setBrightness(g_brightStep);
        drawMenuPanel(MENU_X);
      }
      return;
    }
    return;
  }

  if (g_scr == SCR_NOW) {                              // ---- Now Playing ----
    if (y < 30)    { openMenu(); return; }             // banner = open menu
    if (y >= 250)  { g_scr = SCR_LIST; g_scrDirty = true; return; }  // bottom = playlist
    playerToggle();                                    // card area = play/pause
    return;
  }

  // ---- Playlist ("Start of Story") ----
  if (y < 30) { g_scr = SCR_NOW; g_scrDirty = true; return; }        // header = back
  if (g_trackCount == 0) return;
  for (int k = 0; k < 3; k++) {
    int y0 = ROW_Y0 + k * (ROW_H + 8);
    if (y >= y0 && y < y0 + ROW_H) {
      int idx = g_listScroll + k;
      if (idx >= g_trackCount) return;
      g_listCur = idx;
      listEnsureVisible();
      if (idx == g_curTrack && g_playing) playerToggle();
      else playIdx(idx, true);
      g_scrDirty = true;
      return;
    }
  }
}

static void touchTick()
{
  static uint32_t lastPoll = 0;
  static bool     down     = false;
  uint32_t now = millis();
  if (now - lastPoll < 10) return;
  lastPoll = now;

  int16_t x, y;
  bool pressed = tft.getTouch(&x, &y);
  if (pressed && !down) {                              // new tap
    down = true;
    if (!g_screenOn) {                                 // touch wakes like a button
      tft.setBrightness(g_brightness);
      g_screenOn = true; g_lastInput = millis();
      Serial.println("[ui] wake (touch)");
      return;
    }
    g_lastInput = millis();
    touchTap(x, y);
  } else if (!pressed) {
    down = false;
  }
}

// ================= wake / screen-off / BOOT key =================
static void screenSleep()
{
  tft.setBrightness(0);
  g_screenOn = false;
  Serial.println("[ui] screen off (idle 20 s)  - press any button to wake");
}

static void bootTick()
{
  static uint32_t t0 = 0;                              // BOOT button press start
  if (digitalRead(0) == LOW) {
    if (t0 == 0) t0 = millis();
    if (millis() - t0 > 3000) {                        // long = deep sleep
      Serial.println("[ui] BOOT held 3 s -> deep sleep");
      prefsSave();
      tft.setBrightness(0);
      tft.fillScreen(0);
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
      esp_deep_sleep_start();
    }
  } else {
    t0 = 0;
  }
}

// ================= init + loop-side tick =================
static void fullRedraw()
{
  if (g_scr == SCR_NOW && g_menu == MENU_CLOSED) drawNowStatic();
  if (g_scr == SCR_NOW && g_menu != MENU_CLOSED) { drawNowStatic(); drawMenuPanel(MENU_X); }
  if (g_scr == SCR_LIST) { drawListScreen(); if (g_menu != MENU_CLOSED) drawMenuPanel(MENU_X); }
}
static void uiInit()
{
  pinMode(PIN_PLAY, INPUT_PULLUP);
  pinMode(PIN_HOME, INPUT_PULLUP);
  pinMode(PIN_LADDER, INPUT);
  pinMode(0, INPUT_PULLUP);              // BOOT button (long-press = deep sleep)
  analogSetAttenuation(ADC_11db);
  memset(s_run, 0, sizeof(s_run));

  tft.init();
  tft.setRotation(UI_ROTATION);
#if PANEL_VARIANT == 1
  Serial.println("[ui] panel driver: ST7789 (variant 1)");
#elif PANEL_VARIANT == 2
  Serial.println("[ui] panel driver: ILI9341 + rgb-swap (variant 2)");
#else
  Serial.println("[ui] panel driver: ILI9341 (variant 0)");
#endif
  Serial.println("[ui] touch: XPT2046 (CS33, IRQ36, shared bus)");
  tft.setBrightness(g_brightness);
  tft.fillScreen(C_BG);
  tft.setFont(F_B12);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(CC_DATUM);
  tft.drawString("Starting...", 120, 160);
  tft.setTextDatum(TL_DATUM);
  prefsSave();
}

static void uiTick()
{
  uint32_t now = millis();
  static uint32_t lastProbe = 0;
  if (now - lastProbe >= 5) {
    lastProbe = now;
    inputTick();
    while (g_qTail != g_qHead) {
      InEvt e = g_q[g_qTail];
      g_qTail = (g_qTail + 1) % 10;
      handleEvent(e.btn, e.kind);
    }
    touchTick();
  }

  bootTick();

  // redraws
  if (g_scrDirty) { g_scrDirty = false; fullRedraw(); }

  if (g_uiTrackDirty && g_screenOn) {
    g_uiTrackDirty = false;
    if (g_scr == SCR_NOW && g_menu == MENU_CLOSED) { drawNowStatic(); }
    if (g_scr == SCR_LIST) { g_scrDirty = true; }
  }

  if (g_uiWifiDirty) {
    g_uiWifiDirty = false;
    if (g_menu == MENU_WIFI) drawMenuPanel(MENU_X);
  }

  // list duration/progress only on the NOW screen
  static uint32_t lastBar = 0;
  if (g_screenOn && g_menu == MENU_CLOSED && g_scr == SCR_NOW && now - lastBar >= 1000) {
    lastBar = now;
    drawNowDynamic();
  }

  // toast expiry
  if (g_toastEnd && now >= g_toastEnd) {
    g_toastEnd = 0;
    if (g_menu == MENU_CLOSED && g_scr == SCR_NOW) drawNowDynamic();
    else if (g_menu == MENU_CLOSED && g_scr == SCR_LIST) drawListScreen();
    else if (g_menu != MENU_CLOSED) drawMenuPanel(MENU_X);
  }

  // auto screen-off
  if (g_screenOn && now - g_lastInput >= SCREEN_OFF_MS &&
      g_menu == MENU_CLOSED && g_wifiStage != W_PROGRESS) {
    screenSleep();
  }
}
