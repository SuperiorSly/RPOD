// ============================================================================
//  ui.cpp  -  the whole screen: Now Playing, PlayList, sidebar menu, themes,
//             brightness, WiFi page.  LVGL 8.3, portrait 240x320.
// ============================================================================
#include "ui.h"
#include "config.h"
#include "display.h"
#include "buttons.h"
#include "library.h"
#include "player.h"
#include "settings.h"
#include "telegram.h"
#include "sdcard.h"
#include <lvgl.h>

// ---- generated intro assets (plain-C files: font_pacifico_32.c, heart_img.c) ----
#if INTRO_ENABLED
extern "C" {
  LV_FONT_DECLARE(font_pacifico_32);
  LV_IMG_DECLARE(heart_img);
}
#endif

// ------------------------------------------------------------------ themes --
struct Theme { const char* name; uint32_t accent, bg, surface, border; };
static const Theme THEMES[] = {
  { "Green",  0x22C55E, 0x0B0F14, 0x151B23, 0x3B4656 },
  { "Ocean",  0x38BDF8, 0x0A0E17, 0x141B27, 0x38465C },
  { "Ruby",   0xF43F5E, 0x120B0E, 0x1F1519, 0x5A3A42 },
  { "Violet", 0xA78BFA, 0x0E0B16, 0x181424, 0x473C63 },
  { "Sunset", 0xFB923C, 0x120E0A, 0x201913, 0x5C4A38 },
  { "Mint",   0x2DD4BF, 0x0A1212, 0x132120, 0x35605C },
  { "Gold",   0xFACC15, 0x100F08, 0x1E1C10, 0x5E5628 },
  { "Mono",   0xE5E7EB, 0x0B0B0B, 0x181818, 0x4A4A4A },
};
#define THEME_N ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

#define COL_TEXT   0xFFFFFF
#define COL_SUB    0x9CA3AF

// ------------------------------------------------------------------ styles --
static lv_style_t sScreen, sText, sSurface, sHeader, sRow, sRowSel, sItem, sItemSel, sItemEdit,
                  sPanel, sTrack, sFill, sAccentTxt, sAccentBg, sPill;
static lv_style_transition_dsc_t trans;
static const lv_style_prop_t transProps[] = { LV_STYLE_BORDER_COLOR, LV_STYLE_BG_COLOR, LV_STYLE_PROP_INV };

static void applyTheme(int idx) {
  if (idx < 0 || idx >= THEME_N) idx = 0;
  const Theme& t = THEMES[idx];
  lv_style_set_bg_color(&sScreen, lv_color_hex(t.bg));
  lv_style_set_bg_color(&sSurface, lv_color_hex(t.surface));
  lv_style_set_border_color(&sSurface, lv_color_hex(t.border));
  lv_style_set_bg_color(&sHeader, lv_color_hex(t.surface));
  lv_style_set_border_color(&sHeader, lv_color_hex(t.bg));
  lv_style_set_bg_color(&sRow, lv_color_hex(t.bg));
  lv_style_set_border_color(&sRow, lv_color_hex(t.border));
  lv_style_set_bg_color(&sRowSel, lv_color_hex(t.surface));
  lv_style_set_border_color(&sRowSel, lv_color_hex(t.accent));
  lv_style_set_bg_color(&sItem, lv_color_hex(t.bg));
  lv_style_set_border_color(&sItem, lv_color_hex(t.border));
  lv_style_set_bg_color(&sItemSel, lv_color_hex(t.surface));
  lv_style_set_border_color(&sItemSel, lv_color_hex(t.accent));
  lv_style_set_bg_color(&sItemEdit, lv_color_hex(t.surface));
  lv_style_set_border_color(&sItemEdit, lv_color_hex(t.accent));
  lv_style_set_bg_color(&sPanel, lv_color_hex(t.bg));
  lv_style_set_border_color(&sPanel, lv_color_hex(t.border));
  lv_style_set_bg_color(&sTrack, lv_color_hex(0x4B5563));
  lv_style_set_text_color(&sAccentTxt, lv_color_hex(t.accent));
  lv_style_set_bg_color(&sAccentBg, lv_color_hex(t.accent));
  lv_obj_report_style_change(NULL);
}

static void initStyles() {
  lv_style_transition_dsc_init(&trans, transProps, lv_anim_path_linear, 150, 0, NULL);

  lv_style_init(&sScreen);
  lv_style_set_bg_opa(&sScreen, LV_OPA_COVER);
  lv_style_set_text_color(&sScreen, lv_color_hex(COL_TEXT));
  lv_style_set_text_font(&sScreen, &lv_font_montserrat_14);

  lv_style_init(&sText);
  lv_style_set_text_color(&sText, lv_color_hex(COL_TEXT));
  lv_style_set_text_font(&sText, &lv_font_montserrat_14);

  lv_style_init(&sSurface);
  lv_style_set_bg_opa(&sSurface, LV_OPA_COVER);
  lv_style_set_border_width(&sSurface, 1);
  lv_style_set_radius(&sSurface, 14);

  lv_style_init(&sHeader);
  lv_style_set_bg_opa(&sHeader, LV_OPA_COVER);
  lv_style_set_border_width(&sHeader, 2);
  lv_style_set_border_side(&sHeader, LV_BORDER_SIDE_BOTTOM);

  lv_style_init(&sRow);
  lv_style_set_bg_opa(&sRow, LV_OPA_COVER);
  lv_style_set_border_width(&sRow, 1);
  lv_style_set_radius(&sRow, 12);
  lv_style_set_transition(&sRow, &trans);

  lv_style_init(&sRowSel);
  lv_style_set_border_width(&sRowSel, 2);

  lv_style_init(&sItem);
  lv_style_set_bg_opa(&sItem, LV_OPA_COVER);
  lv_style_set_border_width(&sItem, 1);
  lv_style_set_radius(&sItem, 6);
  lv_style_set_transition(&sItem, &trans);

  lv_style_init(&sItemSel);
  lv_style_set_border_width(&sItemSel, 2);
  lv_style_init(&sItemEdit);
  lv_style_set_border_width(&sItemEdit, 2);

  lv_style_init(&sPanel);
  lv_style_set_bg_opa(&sPanel, LV_OPA_COVER);
  lv_style_set_border_width(&sPanel, 1);
  lv_style_set_border_side(&sPanel, LV_BORDER_SIDE_LEFT);

  lv_style_init(&sTrack);
  lv_style_set_bg_opa(&sTrack, LV_OPA_COVER);
  lv_style_set_radius(&sTrack, LV_RADIUS_CIRCLE);

  lv_style_init(&sFill);
  lv_style_set_bg_opa(&sFill, LV_OPA_COVER);
  lv_style_set_bg_color(&sFill, lv_color_hex(0xFFFFFF));
  lv_style_set_radius(&sFill, LV_RADIUS_CIRCLE);

  lv_style_init(&sAccentTxt);
  lv_style_init(&sAccentBg);
  lv_style_set_bg_opa(&sAccentBg, LV_OPA_COVER);
  lv_style_set_radius(&sAccentBg, LV_RADIUS_CIRCLE);

  lv_style_init(&sPill);
  lv_style_set_bg_opa(&sPill, LV_OPA_80);
  lv_style_set_bg_color(&sPill, lv_color_hex(0x000000));
  lv_style_set_radius(&sPill, 14);

  applyTheme(gSettings.theme);
}

// ---------------------------------------------------------------- helpers ---
static lv_obj_t* mkObj(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}
static lv_obj_t* mkLabel(lv_obj_t* parent, int x, int y, int w, const lv_font_t* f, const char* txt) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_remove_style_all(l);
  lv_obj_add_style(l, &sText, 0);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_pos(l, x, y);
  if (w > 0) lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_label_set_text(l, txt);
  return l;
}
static void greyText(lv_obj_t* l) { lv_obj_set_style_text_color(l, lv_color_hex(COL_SUB), 0); }
static lv_obj_t* mkBar(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* b = lv_bar_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_add_style(b, &sTrack, LV_PART_MAIN);
  lv_obj_add_style(b, &sFill, LV_PART_INDICATOR);
  lv_bar_set_range(b, 0, 1000);
  lv_bar_set_value(b, 0, LV_ANIM_OFF);
  return b;
}
static void setIf(lv_obj_t* l, char* cache, size_t n, const char* txt) {
  if (strncmp(cache, txt, n - 1)) { strncpy(cache, txt, n - 1); cache[n - 1] = 0; lv_label_set_text(l, txt); }
}

// -------------------------------------------------------------- UI state ----
static lv_obj_t *scrNow, *scrList, *scrSplash;
static bool      onList = false;

// Now Playing widgets
static lv_obj_t *nowCover, *nowPh, *nowTitle, *nowArtist, *nowBar, *nowElapsed, *nowTotal, *nowIconL, *nowIconR;
static char      cTitle[48], cArtist[40], cElapsed[12], cTotal[12];
static int       lastSec = -1, lastBar = -1;

// Playlist widgets
#define SLOTS      5
#define ROW_H      80
#define ROW_PITCH  86
#define LIST_TOP   44
#define LIST_H     (SCREEN_H - LIST_TOP)
struct RowSlot { lv_obj_t *row, *thumb, *ph, *title, *artist, *dur, *ind; int bound; };
static RowSlot   slot[SLOTS];
static lv_obj_t *listArea, *listEmpty, *listScroll;
static int       sel = 0, listOff = 0;

// Overlays
static lv_obj_t *volPill, *volBar, *volLbl, *toastBox, *toastLbl;
static uint32_t  volHideAt = 0, toastHideAt = 0;

// Menu
static lv_obj_t *dimmer, *panel, *pgMain, *pgThemes, *pgWifi;
struct MItem { lv_obj_t *box, *title, *sub, *bar, *pct; };
static MItem     mi[5];
static const char* MI_NAMES[5] = { "WiFi", "Shuffle Playlist", "Brightness", "Bluetooth", "Settings" };
enum { MI_WIFI, MI_SHUFFLE, MI_BRIGHT, MI_BT, MI_SETTINGS };
struct TRow { lv_obj_t *box, *name, *chk; };
static TRow      tr[THEME_N];
static int       menuLevel = 0;          // 0 closed, 1 main, 2 themes, 3 wifi
static int       menuFocus = 0, themeFocus = 0;
static bool      brightEdit = false;
static lv_obj_t *wStatus, *wName, *wBar, *wPct, *wAdded, *wLast, *wHint;
static char      wcStatus[64], wcName[64], wcAdded[24], wcLast[64], wcPct[8];
static char      mcWifi[24];

// input
static uint32_t  lastInputMs = 0, lastSeekMs = 0;
static uint8_t   swallowMask = 0;
static uint8_t   tapCnt[2] = {0, 0};
static uint32_t  tapAt[2]  = {0, 0};

// ------------------------------------------------------------ small anims ---
static void animXcb(void* o, int32_t v) { lv_obj_set_x((lv_obj_t*)o, v); }
static void animOpaCb(void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void animListCb(void*, int32_t v) { listOff = v; extern void layoutList(); layoutList(); }

static void animX(lv_obj_t* o, int from, int to, int ms, lv_anim_path_cb_t path, lv_anim_ready_cb_t ready = nullptr) {
  lv_anim_del(o, animXcb);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, animXcb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_path_cb(&a, path);
  if (ready) lv_anim_set_ready_cb(&a, ready);
  lv_anim_start(&a);
}
static void animOpa(lv_obj_t* o, int from, int to, int ms) {
  lv_anim_del(o, animOpaCb);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, animOpaCb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

void uiToast(const char* text) {
  if (!toastBox) return;
  lv_label_set_text(toastLbl, text);
  lv_obj_clear_flag(toastBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toastBox);
  toastHideAt = millis() + 2200;
}

static void showVolume() {
  lv_bar_set_value(volBar, gSettings.volume * 10, LV_ANIM_OFF);
  lv_label_set_text_fmt(volLbl, LV_SYMBOL_VOLUME_MAX " %d%%", gSettings.volume);
  lv_obj_clear_flag(volPill, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(volPill);
  volHideAt = millis() + 1300;
}

// ============================================================ NOW PLAYING ===
static void buildNow() {
  scrNow = lv_obj_create(NULL);
  lv_obj_remove_style_all(scrNow);
  lv_obj_add_style(scrNow, &sScreen, 0);
  lv_obj_clear_flag(scrNow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* h = mkLabel(scrNow, 0, 9, SCREEN_W, &lv_font_montserrat_16, "Now Playing");
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  nowIconL = mkLabel(scrNow, 12, 11, 40, &lv_font_montserrat_12, "");
  lv_obj_add_style(nowIconL, &sAccentTxt, 0);
  nowIconR = mkLabel(scrNow, SCREEN_W - 12 - 60, 11, 60, &lv_font_montserrat_12, "");
  lv_obj_set_style_text_align(nowIconR, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_add_style(nowIconR, &sAccentTxt, 0);

  const int cx = (SCREEN_W - COVER_PX) / 2;
  lv_obj_t* box = mkObj(scrNow, cx, 36, COVER_PX, COVER_PX);
  lv_obj_add_style(box, &sSurface, 0);
  lv_obj_set_style_clip_corner(box, true, 0);
  nowPh = mkLabel(box, 0, 0, COVER_PX, &lv_font_montserrat_28, LV_SYMBOL_AUDIO);
  lv_obj_set_style_text_align(nowPh, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(nowPh, lv_color_hex(0x4B5563), 0);
  lv_obj_set_style_pad_top(nowPh, COVER_PX / 2 - 16, 0);
  nowCover = lv_img_create(box);
  lv_obj_remove_style_all(nowCover);
  lv_obj_set_pos(nowCover, -1, -1);
  lv_obj_add_flag(nowCover, LV_OBJ_FLAG_HIDDEN);

  nowTitle = mkLabel(scrNow, cx, 222, COVER_PX, &lv_font_montserrat_20, "");
  lv_label_set_long_mode(nowTitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
  nowArtist = mkLabel(scrNow, cx, 248, COVER_PX, &lv_font_montserrat_14, "");
  lv_label_set_long_mode(nowArtist, LV_LABEL_LONG_DOT);
  greyText(nowArtist);

  nowBar = mkBar(scrNow, cx, 277, COVER_PX, 6);
  nowElapsed = mkLabel(scrNow, cx, 289, 70, &lv_font_montserrat_12, "0:00");
  nowTotal = mkLabel(scrNow, cx + COVER_PX - 70, 289, 70, &lv_font_montserrat_12, "0:00");
  lv_obj_set_style_text_align(nowTotal, LV_TEXT_ALIGN_RIGHT, 0);
}

static void updateNow() {
  int c = playerCurrent();
  lastSec = -1; lastBar = -1;
  if (c < 0) {
    setIf(nowTitle, cTitle, sizeof cTitle, "No songs yet");
    setIf(nowArtist, cArtist, sizeof cArtist, "Menu > WiFi to add songs");
    setIf(nowTotal, cTotal, sizeof cTotal, "0:00");
    lv_obj_add_flag(nowCover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nowPh, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const Song& s = librarySong(c);
  lv_label_set_text(nowTitle, s.title); strncpy(cTitle, s.title, sizeof cTitle - 1);
  lv_label_set_text(nowArtist, s.artist); strncpy(cArtist, s.artist, sizeof cArtist - 1);
  char t[12]; fmtTime(s.durSec, t, sizeof t);
  setIf(nowTotal, cTotal, sizeof cTotal, t);
  if (s.hasCover) {
    char p[40]; libraryCoverPath(s, p, sizeof p, false);
    lv_img_set_src(nowCover, p);
    lv_obj_clear_flag(nowCover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(nowPh, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(nowCover, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nowPh, LV_OBJ_FLAG_HIDDEN);
  }
}

static void updateNowTime() {
  uint32_t ms = playerPosMs();
  int sec = ms / 1000;
  if (sec != lastSec) {
    lastSec = sec;
    char t[12]; fmtTime(sec, t, sizeof t);
    setIf(nowElapsed, cElapsed, sizeof cElapsed, t);
  }
  uint32_t d = playerDurSec() * 1000UL;
  int v = d ? (int)((uint64_t)ms * 1000 / d) : 0;
  if (v > 1000) v = 1000;
  if (v != lastBar) { lastBar = v; lv_bar_set_value(nowBar, v, LV_ANIM_OFF); }
}

static void updateIcons() {
  char l[16] = "", r[24] = "";
  PlayerState ps = playerState();
  strcpy(l, ps == PS_PLAYING ? LV_SYMBOL_PLAY : ps == PS_PAUSED ? LV_SYMBOL_PAUSE : LV_SYMBOL_STOP);
  if (gSettings.shuffle) strcat(r, LV_SYMBOL_SHUFFLE " ");
  if (telegramActive()) strcat(r, LV_SYMBOL_WIFI);
  static char cl[16], cr[24];
  if (strcmp(cl, l)) { strcpy(cl, l); lv_label_set_text(nowIconL, l); }
  if (strcmp(cr, r)) { strcpy(cr, r); lv_label_set_text(nowIconR, r); }
}

// ================================================================ PLAYLIST ===
static void bindRow(RowSlot& s, int idx) {
  s.bound = idx;
  const Song& g = librarySong(idx);
  lv_label_set_text(s.title, g.title);
  lv_label_set_text(s.artist, g.artist);
  char t[12]; fmtTime(g.durSec, t, sizeof t);
  lv_label_set_text(s.dur, t);
  if (g.hasCover) {
    char p[40]; libraryCoverPath(g, p, sizeof p, true);
    lv_img_set_src(s.thumb, p);
    lv_obj_clear_flag(s.thumb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s.ph, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s.thumb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s.ph, LV_OBJ_FLAG_HIDDEN);
  }
}

static void styleRow(RowSlot& s) {
  if (s.bound < 0) return;
  bool isSel = (s.bound == sel), isCur = (s.bound == playerCurrent());
  if (isSel) lv_obj_add_state(s.row, LV_STATE_USER_1); else lv_obj_clear_state(s.row, LV_STATE_USER_1);
  if (isCur) lv_obj_add_state(s.title, LV_STATE_USER_2); else lv_obj_clear_state(s.title, LV_STATE_USER_2);
  lv_label_set_long_mode(s.title, isSel ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
  if (isCur) {
    lv_label_set_text(s.ind, playerState() == PS_PAUSED ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_clear_flag(s.ind, LV_OBJ_FLAG_HIDDEN);
  } else lv_obj_add_flag(s.ind, LV_OBJ_FLAG_HIDDEN);
}

void layoutList() {
  int n = libraryCount();
  lv_obj_t* e = listEmpty;
  if (n == 0) { lv_obj_clear_flag(e, LV_OBJ_FLAG_HIDDEN); } else lv_obj_add_flag(e, LV_OBJ_FLAG_HIDDEN);
  int base = listOff / ROW_PITCH;
  for (int k = 0; k < SLOTS; k++) if (slot[k].bound >= n) { lv_obj_add_flag(slot[k].row, LV_OBJ_FLAG_HIDDEN); slot[k].bound = -1; }
  for (int k = 0; k < SLOTS; k++) {
    int idx = base + k;
    if (idx >= n) continue;
    RowSlot& s = slot[idx % SLOTS];
    if (s.bound != idx) { bindRow(s, idx); lv_obj_clear_flag(s.row, LV_OBJ_FLAG_HIDDEN); styleRow(s); }
    lv_obj_set_y(s.row, 4 + idx * ROW_PITCH - listOff);
  }
  // scroll indicator
  int total = n * ROW_PITCH;
  if (total > LIST_H) {
    int bh = LIST_H * LIST_H / total; if (bh < 20) bh = 20;
    int by = (LIST_H - bh) * listOff / (total - LIST_H);
    lv_obj_set_size(listScroll, 3, bh); lv_obj_set_y(listScroll, LIST_TOP + by);
    lv_obj_clear_flag(listScroll, LV_OBJ_FLAG_HIDDEN);
  } else lv_obj_add_flag(listScroll, LV_OBJ_FLAG_HIDDEN);
}

static void refreshRows() { for (int k = 0; k < SLOTS; k++) styleRow(slot[k]); }

static int maxOff() { int m = libraryCount() * ROW_PITCH - LIST_H + 4; return m > 0 ? m : 0; }

static void scrollToSel(bool animate) {
  int n = libraryCount();
  if (n == 0) { listOff = 0; layoutList(); return; }
  int top = sel * ROW_PITCH, bot = top + ROW_H + 8;
  int off = listOff;
  if (top < off) off = top;
  else if (bot > off + LIST_H) off = bot - LIST_H;
  if (off > maxOff()) off = maxOff();
  if (off < 0) off = 0;
  if (!animate) { lv_anim_del(nullptr, animListCb); listOff = off; layoutList(); return; }
  if (off == listOff) return;
  lv_anim_del(nullptr, animListCb);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, &listOff);
  lv_anim_set_exec_cb(&a, animListCb);
  lv_anim_set_values(&a, listOff, off);
  lv_anim_set_time(&a, 160);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void rebuildList() {
  for (int k = 0; k < SLOTS; k++) { slot[k].bound = -1; lv_obj_add_flag(slot[k].row, LV_OBJ_FLAG_HIDDEN); }
  int n = libraryCount();
  if (sel >= n) sel = n ? n - 1 : 0;
  if (listOff > maxOff()) listOff = maxOff();
  layoutList();
  refreshRows();
}

static void buildList() {
  scrList = lv_obj_create(NULL);
  lv_obj_remove_style_all(scrList);
  lv_obj_add_style(scrList, &sScreen, 0);
  lv_obj_clear_flag(scrList, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* hd = mkObj(scrList, 0, 0, SCREEN_W, 40);
  lv_obj_add_style(hd, &sHeader, 0);
  lv_obj_t* t = mkLabel(hd, 0, 11, SCREEN_W, &lv_font_montserrat_16, "PlayList");
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);

  listArea = mkObj(scrList, 0, LIST_TOP, SCREEN_W, LIST_H);
  listEmpty = mkLabel(listArea, 20, 90, SCREEN_W - 40, &lv_font_montserrat_14, "No songs yet.\n\nOpen the menu (Home)\nand choose WiFi to add\nsongs with Telegram.");
  lv_obj_set_style_text_align(listEmpty, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(listEmpty, LV_LABEL_LONG_WRAP);
  greyText(listEmpty);

  for (int k = 0; k < SLOTS; k++) {
    RowSlot& s = slot[k];
    s.bound = -1;
    s.row = mkObj(listArea, 8, 0, SCREEN_W - 16, ROW_H);
    lv_obj_add_style(s.row, &sRow, 0);
    lv_obj_add_style(s.row, &sRowSel, LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_t* tb = mkObj(s.row, 12, 14, THUMB_PX, THUMB_PX);
    lv_obj_add_style(tb, &sSurface, 0);
    lv_obj_set_style_radius(tb, 6, 0);
    lv_obj_set_style_clip_corner(tb, true, 0);
    s.ph = mkLabel(tb, 0, 14, THUMB_PX, &lv_font_montserrat_16, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_align(s.ph, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s.ph, lv_color_hex(0x4B5563), 0);
    s.thumb = lv_img_create(tb);
    lv_obj_remove_style_all(s.thumb);
    lv_obj_set_pos(s.thumb, -1, -1);
    lv_obj_add_flag(s.thumb, LV_OBJ_FLAG_HIDDEN);
    s.title = mkLabel(s.row, 72, 15, 104, &lv_font_montserrat_16, "");
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_DOT);
    lv_obj_add_style(s.title, &sAccentTxt, LV_PART_MAIN | LV_STATE_USER_2);
    s.artist = mkLabel(s.row, 72, 42, 104, &lv_font_montserrat_12, "");
    lv_label_set_long_mode(s.artist, LV_LABEL_LONG_DOT);
    greyText(s.artist);
    s.dur = mkLabel(s.row, SCREEN_W - 16 - 10 - 40, 31, 40, &lv_font_montserrat_14, "");
    lv_obj_set_style_text_align(s.dur, LV_TEXT_ALIGN_RIGHT, 0);
    s.ind = mkLabel(s.row, SCREEN_W - 16 - 10 - 20, 8, 20, &lv_font_montserrat_12, "");
    lv_obj_set_style_text_align(s.ind, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_style(s.ind, &sAccentTxt, 0);
    lv_obj_add_flag(s.row, LV_OBJ_FLAG_HIDDEN);
  }
  listScroll = mkObj(scrList, SCREEN_W - 4, LIST_TOP, 3, 30);
  lv_obj_add_style(listScroll, &sAccentBg, 0);
  lv_obj_add_flag(listScroll, LV_OBJ_FLAG_HIDDEN);
}

// ================================================================== MENU ====
static void mkItem(lv_obj_t* parent, int y, const char* title, MItem& m) {
  m.box = mkObj(parent, 8, y, 128, 46);
  lv_obj_add_style(m.box, &sItem, 0);
  lv_obj_add_style(m.box, &sItemSel, LV_PART_MAIN | LV_STATE_USER_1);
  lv_obj_add_style(m.box, &sItemEdit, LV_PART_MAIN | LV_STATE_USER_2);
  m.title = mkLabel(m.box, 10, 5, 108, &lv_font_montserrat_14, title);
  m.sub = mkLabel(m.box, 10, 25, 108, &lv_font_montserrat_12, "");
  greyText(m.sub);
  lv_obj_add_style(m.sub, &sAccentTxt, LV_PART_MAIN | LV_STATE_USER_2);
  m.bar = mkBar(m.box, 10, 30, 90, 8);
  lv_obj_add_flag(m.bar, LV_OBJ_FLAG_HIDDEN);
  m.pct = mkLabel(m.box, 76, 5, 42, &lv_font_montserrat_12, "");
  lv_obj_set_style_text_align(m.pct, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_add_flag(m.pct, LV_OBJ_FLAG_HIDDEN);
}

static const char* wifiPhaseShort(TgPhase p) {
  switch (p) {
    case TG_CONNECTING:  return "Connecting...";
    case TG_WAITING:     return "On";
    case TG_DOWNLOADING: return "Downloading";
    case TG_PROCESSING:  return "Processing";
    case TG_ERROR:       return "Error";
    case TG_STOPPING:    return "Turning off";
    default:             return "Off";
  }
}

static void setOnOff(lv_obj_t* sub, bool on) {
  lv_label_set_text(sub, on ? "On" : "Off");
  if (on) lv_obj_add_state(sub, LV_STATE_USER_2); else lv_obj_clear_state(sub, LV_STATE_USER_2);
}

static void updateMenuTexts() {
  TgStatus t = telegramStatus();
  const char* w = wifiPhaseShort(t.phase);
  if (strcmp(mcWifi, w)) { strncpy(mcWifi, w, sizeof mcWifi - 1); lv_label_set_text(mi[MI_WIFI].sub, w);
    if (t.phase == TG_OFF || t.phase == TG_STOPPING) lv_obj_clear_state(mi[MI_WIFI].sub, LV_STATE_USER_2); else lv_obj_add_state(mi[MI_WIFI].sub, LV_STATE_USER_2); }
  setOnOff(mi[MI_SHUFFLE].sub, gSettings.shuffle);
  setOnOff(mi[MI_BT].sub, gSettings.bluetooth);
  static int lb = -1, lt = -1;
  if (lb != gSettings.brightness) { lb = gSettings.brightness; lv_label_set_text_fmt(mi[MI_BRIGHT].sub, "%d%%", lb);
    lv_label_set_text_fmt(mi[MI_BRIGHT].pct, "%d%%", lb); lv_bar_set_value(mi[MI_BRIGHT].bar, lb * 10, LV_ANIM_OFF); }
  if (lt != gSettings.theme) { lt = gSettings.theme; lv_label_set_text(mi[MI_SETTINGS].sub, THEMES[gSettings.theme].name); }
}

static void updateMenuFocus() {
  for (int i = 0; i < 5; i++) {
    if (i == menuFocus && menuLevel == 1) lv_obj_add_state(mi[i].box, LV_STATE_USER_1); else lv_obj_clear_state(mi[i].box, LV_STATE_USER_1);
    if (i == MI_BRIGHT && brightEdit) lv_obj_add_state(mi[i].box, LV_STATE_USER_2); else lv_obj_clear_state(mi[i].box, LV_STATE_USER_2);
  }
  bool e = brightEdit;
  if (e) { lv_obj_add_flag(mi[MI_BRIGHT].sub, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(mi[MI_BRIGHT].bar, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(mi[MI_BRIGHT].pct, LV_OBJ_FLAG_HIDDEN); }
  else   { lv_obj_clear_flag(mi[MI_BRIGHT].sub, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(mi[MI_BRIGHT].bar, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(mi[MI_BRIGHT].pct, LV_OBJ_FLAG_HIDDEN); }
  for (int i = 0; i < THEME_N; i++) {
    if (i == themeFocus && menuLevel == 2) lv_obj_add_state(tr[i].box, LV_STATE_USER_1); else lv_obj_clear_state(tr[i].box, LV_STATE_USER_1);
    if (i == gSettings.theme) lv_obj_clear_flag(tr[i].chk, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(tr[i].chk, LV_OBJ_FLAG_HIDDEN);
  }
}

static void updateWifiPage() {
  TgStatus t = telegramStatus();
  const char* s = "WiFi is off";
  switch (t.phase) {
    case TG_CONNECTING:  s = "Connecting to WiFi..."; break;
    case TG_WAITING:     s = "Waiting for songs.\nSend MP3 files to your Telegram bot."; break;
    case TG_DOWNLOADING: s = "Downloading..."; break;
    case TG_PROCESSING:  s = "Reading tags and cover..."; break;
    case TG_ERROR:       s = t.line; break;
    case TG_STOPPING:    s = "Turning WiFi off..."; break;
    default: break;
  }
  setIf(wStatus, wcStatus, sizeof wcStatus, s);
  bool dl = (t.phase == TG_DOWNLOADING || t.phase == TG_PROCESSING);
  setIf(wName, wcName, sizeof wcName, dl ? t.line : "");
  char p[8] = ""; if (t.phase == TG_DOWNLOADING) snprintf(p, sizeof p, "%d%%", t.pct);
  setIf(wPct, wcPct, sizeof wcPct, p);
  static int lp = -1; int pv = dl ? (t.phase == TG_PROCESSING ? 1000 : t.pct * 10) : 0;
  if (lp != pv) { lp = pv; lv_bar_set_value(wBar, pv, LV_ANIM_OFF); }
  char a[24]; snprintf(a, sizeof a, "Songs added: %u", t.added);
  setIf(wAdded, wcAdded, sizeof wcAdded, a);
  setIf(wLast, wcLast, sizeof wcLast, t.added ? t.last : "");
}

static lv_obj_t* pageOf(int level) { return level == 2 ? pgThemes : level == 3 ? pgWifi : pgMain; }

static void gotoPage(int level) {
  if (level == menuLevel) return;
  bool fwd = level > menuLevel;
  lv_obj_t* in = pageOf(level); lv_obj_t* out = pageOf(menuLevel);
  animX(in,  fwd ? 144 : -144, 0, MENU_ANIM_MS, lv_anim_path_ease_out);
  animX(out, 0, fwd ? -144 : 144, MENU_ANIM_MS, lv_anim_path_ease_out);
  menuLevel = level;
  if (level == 2) themeFocus = gSettings.theme;
  updateMenuFocus(); updateMenuTexts();
}

static void hideMenuReady(lv_anim_t*) {
  if (menuLevel == 0) { lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(dimmer, LV_OBJ_FLAG_HIDDEN); }
}

static void openMenu() {
  if (menuLevel) return;
  menuLevel = 1; menuFocus = 0; brightEdit = false;
  lv_obj_set_x(pgMain, 0); lv_obj_set_x(pgThemes, 144); lv_obj_set_x(pgWifi, 144);
  updateMenuTexts(); updateMenuFocus();
  lv_obj_clear_flag(dimmer, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(dimmer); lv_obj_move_foreground(panel);
  animX(panel, lv_obj_get_x(panel), SCREEN_W - 144, MENU_ANIM_MS, lv_anim_path_ease_out);
  animOpa(dimmer, lv_obj_get_style_bg_opa(dimmer, 0), 150, MENU_ANIM_MS);
}

static void closeMenu() {
  if (!menuLevel) return;
  brightEdit = false; menuLevel = 0;
  updateMenuFocus();
  animX(panel, lv_obj_get_x(panel), SCREEN_W, MENU_ANIM_MS, lv_anim_path_ease_in, hideMenuReady);
  animOpa(dimmer, lv_obj_get_style_bg_opa(dimmer, 0), 0, MENU_ANIM_MS);
}

static void buildMenu() {
  lv_obj_t* top = lv_layer_top();
  dimmer = mkObj(top, 0, 0, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(dimmer, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(dimmer, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(dimmer, LV_OBJ_FLAG_HIDDEN);

  panel = mkObj(top, SCREEN_W, 0, 144, SCREEN_H);
  lv_obj_add_style(panel, &sPanel, 0);
  lv_obj_add_style(panel, &sText, 0);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

  // ---- main page
  pgMain = mkObj(panel, 0, 0, 144, SCREEN_H);
  mkLabel(pgMain, 14, 8, 120, &lv_font_montserrat_24, "Menu");
  for (int i = 0; i < 5; i++) mkItem(pgMain, 44 + i * 50, MI_NAMES[i], mi[i]);

  // ---- themes page
  pgThemes = mkObj(panel, 144, 0, 144, SCREEN_H);
  mkLabel(pgThemes, 14, 8, 120, &lv_font_montserrat_24, "Themes");
  for (int i = 0; i < THEME_N; i++) {
    TRow& r = tr[i];
    r.box = mkObj(pgThemes, 8, 44 + i * 33, 128, 30);
    lv_obj_add_style(r.box, &sItem, 0);
    lv_obj_add_style(r.box, &sItemSel, LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_t* sw = mkObj(r.box, 9, 6, 16, 16);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(THEMES[i].accent), 0);
    r.name = mkLabel(r.box, 34, 6, 70, &lv_font_montserrat_14, THEMES[i].name);
    r.chk = mkLabel(r.box, 104, 7, 20, &lv_font_montserrat_12, LV_SYMBOL_OK);
    lv_obj_add_style(r.chk, &sAccentTxt, 0);
  }

  // ---- WiFi page
  pgWifi = mkObj(panel, 144, 0, 144, SCREEN_H);
  mkLabel(pgWifi, 14, 8, 120, &lv_font_montserrat_24, "WiFi");
  wStatus = mkLabel(pgWifi, 10, 48, 126, &lv_font_montserrat_14, "");
  lv_label_set_long_mode(wStatus, LV_LABEL_LONG_WRAP);
  wName = mkLabel(pgWifi, 10, 104, 126, &lv_font_montserrat_12, "");
  lv_label_set_long_mode(wName, LV_LABEL_LONG_SCROLL_CIRCULAR);
  greyText(wName);
  wBar = mkBar(pgWifi, 10, 126, 124, 8);
  wPct = mkLabel(pgWifi, 10, 138, 124, &lv_font_montserrat_12, "");
  lv_obj_set_style_text_align(wPct, LV_TEXT_ALIGN_RIGHT, 0);
  wAdded = mkLabel(pgWifi, 10, 166, 126, &lv_font_montserrat_14, "");
  wLast = mkLabel(pgWifi, 10, 186, 126, &lv_font_montserrat_12, "");
  lv_label_set_long_mode(wLast, LV_LABEL_LONG_WRAP);
  lv_obj_add_style(wLast, &sAccentTxt, 0);
  wHint = mkLabel(pgWifi, 10, 252, 126, &lv_font_montserrat_12, "Play = turn WiFi off\nBack = keep it running");
  lv_label_set_long_mode(wHint, LV_LABEL_LONG_WRAP);
  greyText(wHint);
}

static void buildOverlays() {
  lv_obj_t* top = lv_layer_top();
  volPill = mkObj(top, (SCREEN_W - 150) / 2, 44, 150, 28);
  lv_obj_add_style(volPill, &sPill, 0);
  lv_obj_add_style(volPill, &sText, 0);
  volLbl = mkLabel(volPill, 12, 6, 56, &lv_font_montserrat_14, "");
  volBar = mkBar(volPill, 74, 10, 66, 8);
  lv_bar_set_range(volBar, 0, 1000);
  lv_obj_add_flag(volPill, LV_OBJ_FLAG_HIDDEN);

  toastBox = mkObj(top, 16, SCREEN_H - 78, SCREEN_W - 32, 52);
  lv_obj_add_style(toastBox, &sPill, 0);
  lv_obj_add_style(toastBox, &sText, 0);
  lv_obj_set_style_bg_opa(toastBox, LV_OPA_90, 0);
  toastLbl = mkLabel(toastBox, 10, 8, SCREEN_W - 52, &lv_font_montserrat_14, "");
  lv_label_set_long_mode(toastLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(toastLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(toastBox, LV_OBJ_FLAG_HIDDEN);
}

static void buildSplash() {
  scrSplash = lv_obj_create(NULL);
  lv_obj_remove_style_all(scrSplash);
  lv_obj_add_style(scrSplash, &sScreen, 0);
  lv_obj_t* t = mkLabel(scrSplash, 0, 110, SCREEN_W, &lv_font_montserrat_28, "iPod");
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_style(t, &sAccentTxt, 0);
  lv_obj_t* l = mkLabel(scrSplash, 20, 160, SCREEN_W - 40, &lv_font_montserrat_14, "Starting...");
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  greyText(l);
  lv_obj_t* b = mkBar(scrSplash, 40, 190, SCREEN_W - 80, 6);
  lv_obj_set_user_data(scrSplash, l);
  lv_obj_set_user_data(l, b);
}

void uiSplash(const char* line, int pct) {
  if (lv_scr_act() != scrSplash) lv_scr_load(scrSplash);
  lv_obj_t* l = (lv_obj_t*)lv_obj_get_user_data(scrSplash);
  lv_obj_t* b = (lv_obj_t*)lv_obj_get_user_data(l);
  lv_label_set_text(l, line);
  lv_bar_set_value(b, pct * 10, LV_ANIM_OFF);
  lv_refr_now(NULL);
}

// ================================================================== INTRO ====
#if INTRO_ENABLED
// animation callbacks (only text opa / img opa / bg opa / obj opa / y)
static void introTextOpaCb(void* v, int32_t o) { lv_obj_set_style_text_opa((lv_obj_t*)v, (lv_opa_t)o, 0); }
static void introImgOpaCb(void* v, int32_t o)  { lv_obj_set_style_img_opa((lv_obj_t*)v, (lv_opa_t)o, 0); }
static void introBgOpaCb(void* v, int32_t o)   { lv_obj_set_style_bg_opa((lv_obj_t*)v, (lv_opa_t)o, 0); }
static void introBoxOpaCb(void* v, int32_t o)  { lv_obj_set_style_opa((lv_obj_t*)v, (lv_opa_t)o, 0); }
static void introYCb(void* v, int32_t y)       { lv_obj_set_y((lv_obj_t*)v, y); }

void uiIntro() {
  uint32_t heapBefore = ESP.getFreeHeap();
  uint32_t t0 = millis();

  // ---- own screen; everything sits inside ONE container = one fade target ---
  lv_obj_t* scrIntro = lv_obj_create(NULL);
  lv_obj_remove_style_all(scrIntro);
  lv_obj_add_style(scrIntro, &sScreen, 0);
  lv_obj_clear_flag(scrIntro, LV_OBJ_FLAG_SCROLLABLE);
  lv_scr_load(scrIntro);

  lv_obj_t* box = mkObj(scrIntro, 0, 0, SCREEN_W, SCREEN_H);

  // ---- measure the words; too wide -> lift both, where the heart is wider ----
  lv_point_t sz;
  lv_txt_get_size(&sz, "Hello", &font_pacifico_32, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int helloW = sz.x, helloH = sz.y;
  lv_txt_get_size(&sz, INTRO_NAME, &font_pacifico_32, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  int nameW = sz.x, nameH = sz.y;
  Serial.printf("intro widths: Hello=%d px, %s=%d px\n", helloW, INTRO_NAME, nameW);
  int lift = (helloW > 150 || nameW > 135) ? 10 : 0;

  // ---- the heart: 220x200 at (10,60), recolored in the theme accent ---------
  lv_obj_t* img = lv_img_create(box);
  lv_img_set_src(img, &heart_img);
  lv_obj_set_pos(img, 10, 60);
  lv_obj_set_style_img_recolor(img, lv_color_hex(THEMES[gSettings.theme].accent), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_set_style_img_opa(img, LV_OPA_TRANSP, 0);      // starts invisible

  // ---- Pacifico words, absolutely positioned so y can be animated ----------
  int helloY = 118 - helloH / 2 - lift;
  int nameY  = 166 - nameH  / 2 - lift;
  lv_obj_t* hello = mkLabel(box, 0, helloY + 14, SCREEN_W, &font_pacifico_32, "Hello");
  lv_obj_set_style_text_align(hello, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_opa(hello, LV_OPA_TRANSP, 0);

  lv_obj_t* nameL = mkLabel(box, 0, nameY + 14, SCREEN_W, &font_pacifico_32, INTRO_NAME);
  lv_obj_set_style_text_align(nameL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_style(nameL, &sAccentTxt, 0);
  lv_obj_set_style_text_opa(nameL, LV_OPA_TRANSP, 0);

  // ---- 4 sparkles outside the heart ----------------------------------------
  static const int spXY[4][2] = { {16,72}, {222,100}, {28,270}, {206,255} };
  lv_obj_t* sp[4];
  for (int i = 0; i < 4; i++) {
    sp[i] = mkObj(box, spXY[i][0], spXY[i][1], 6, 6);
    lv_obj_add_style(sp[i], &sAccentBg, 0);             // accent colour + circle shape
    lv_obj_set_style_bg_opa(sp[i], LV_OPA_TRANSP, 0);
  }

  // ---- animations -----------------------------------------------------------
  lv_anim_t a;
  // heart: fade in 0..500 ms ...
  lv_anim_init(&a); lv_anim_set_var(&a, img); lv_anim_set_exec_cb(&a, introImgOpaCb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_90); lv_anim_set_time(&a, 500); // fades to 90%: a bit dimmer
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);
  // ... then heartbeat from 900 ms: 229 <-> 150, 450 ms each way, endless
  lv_anim_init(&a); lv_anim_set_var(&a, img); lv_anim_set_exec_cb(&a, introImgOpaCb);
  lv_anim_set_values(&a, LV_OPA_90, 150); lv_anim_set_time(&a, 450);
  lv_anim_set_delay(&a, 900); lv_anim_set_playback_time(&a, 450);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);

  // "Hello": fade + slide up 14 px, from 300 ms, 500 ms
  lv_anim_init(&a); lv_anim_set_var(&a, hello); lv_anim_set_exec_cb(&a, introTextOpaCb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER); lv_anim_set_time(&a, 500);
  lv_anim_set_delay(&a, 300); lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);
  lv_anim_init(&a); lv_anim_set_var(&a, hello); lv_anim_set_exec_cb(&a, introYCb);
  lv_anim_set_values(&a, helloY + 14, helloY); lv_anim_set_time(&a, 500);
  lv_anim_set_delay(&a, 300); lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);

  // name: same entrance, from 650 ms, 600 ms
  lv_anim_init(&a); lv_anim_set_var(&a, nameL); lv_anim_set_exec_cb(&a, introTextOpaCb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER); lv_anim_set_time(&a, 600);
  lv_anim_set_delay(&a, 650); lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);
  lv_anim_init(&a); lv_anim_set_var(&a, nameL); lv_anim_set_exec_cb(&a, introYCb);
  lv_anim_set_values(&a, nameY + 14, nameY); lv_anim_set_time(&a, 600);
  lv_anim_set_delay(&a, 650); lv_anim_set_path_cb(&a, lv_anim_path_ease_out); lv_anim_start(&a);

  // sparkles: 0 -> 255 -> 0 every 900 ms, endless, staggered by 250 ms
  for (int i = 0; i < 4; i++) {
    lv_anim_init(&a); lv_anim_set_var(&a, sp[i]); lv_anim_set_exec_cb(&a, introBgOpaCb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER); lv_anim_set_time(&a, 450);
    lv_anim_set_delay(&a, i * 250); lv_anim_set_playback_time(&a, 450);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
  }

  // ---- run until INTRO_MS; any button press skips instantly -----------------
  bool skip = false, fading = false;
  while (millis() - t0 < INTRO_MS) {
    buttonsPoll();
    BtnEvent e;
    while (buttonsGet(e)) { if (e.type == BE_PRESS) skip = true; }
    if (skip) break;
    if (!fading && millis() - t0 >= INTRO_MS - 300) {   // final 300 ms: stop loops, fade all
      fading = true;
      lv_anim_del(img, NULL);                           // heartbeat first, then re-use the object
      for (int i = 0; i < 4; i++) lv_anim_del(sp[i], NULL);
      lv_anim_init(&a); lv_anim_set_var(&a, img); lv_anim_set_exec_cb(&a, introImgOpaCb);
      lv_anim_set_values(&a, lv_obj_get_style_img_opa(img, 0), LV_OPA_TRANSP);
      lv_anim_set_time(&a, 300); lv_anim_start(&a);
      lv_anim_init(&a); lv_anim_set_var(&a, hello); lv_anim_set_exec_cb(&a, introTextOpaCb);
      lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP); lv_anim_set_time(&a, 300); lv_anim_start(&a);
      lv_anim_init(&a); lv_anim_set_var(&a, nameL); lv_anim_set_exec_cb(&a, introTextOpaCb);
      lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP); lv_anim_set_time(&a, 300); lv_anim_start(&a);
      for (int i = 0; i < 4; i++) {
        lv_anim_init(&a); lv_anim_set_var(&a, sp[i]); lv_anim_set_exec_cb(&a, introBgOpaCb);
        lv_anim_set_values(&a, lv_obj_get_style_bg_opa(sp[i], 0), LV_OPA_TRANSP);
        lv_anim_set_time(&a, 300); lv_anim_start(&a);
      }
      lv_anim_init(&a); lv_anim_set_var(&a, box); lv_anim_set_exec_cb(&a, introBoxOpaCb);
      lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP); lv_anim_set_time(&a, 300); lv_anim_start(&a);
    }
    lv_timer_handler();
    delay(5);
  }

  // ---- clean up: per-object anim kill (no global del!), then free memory ----
  lv_anim_del(box, NULL); lv_anim_del(img, NULL); lv_anim_del(hello, NULL); lv_anim_del(nameL, NULL);
  for (int i = 0; i < 4; i++) lv_anim_del(sp[i], NULL);

  uiSplash("Loading music...", 0);          // hand over to the real boot screen first
  lv_obj_del(scrIntro);                     // then free the intro screen (no longer active)

  Serial.printf("intro heap %u -> %u\n", heapBefore, ESP.getFreeHeap());
}
#endif // INTRO_ENABLED

void uiInit() {
  initStyles();
  buildNow(); buildList(); buildMenu(); buildOverlays(); buildSplash();
  lv_obj_t* top = lv_layer_top();
  (void)top;
  updateMenuTexts(); updateMenuFocus();
}

void uiLibraryChanged() {
  rebuildList();
  updateNow();
}

void uiStart() {
  rebuildList();
  updateNow(); updateNowTime(); updateIcons();
  onList = false;
  lv_scr_load(scrNow);
  lastInputMs = millis();
  displayOn();
}

// ================================================================= INPUT ====
static void showScreen(bool list) {
  if (list == onList) return;
  onList = list;
  if (list) {
    sel = playerCurrent() >= 0 ? playerCurrent() : 0;
    listOff = 0; rebuildList(); scrollToSel(false);
  }
  if (SCREEN_ANIM_MS > 0)
    lv_scr_load_anim(list ? scrList : scrNow, list ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT, SCREEN_ANIM_MS, 0, false);
  else lv_scr_load(list ? scrList : scrNow);
}

static bool playAllowed() {
  if (telegramActive()) { uiToast("WiFi is on.\nTurn it off first (Menu > WiFi)."); return false; }
  return true;
}

static void changeVolume(int d) {
  playerSetVolume((int)gSettings.volume + d);
  showVolume();
}

static void changeBrightness(int d) {
  int b = (int)gSettings.brightness + d;
  if (b < BRIGHT_MIN_PCT) b = BRIGHT_MIN_PCT;
  if (b > 100) b = 100;
  gSettings.brightness = b; settingsDirty();
  displaySetBrightness(b);
  updateMenuTexts();
}

static void activateItem() {
  switch (menuFocus) {
    case MI_WIFI:
      if (!telegramActive()) { playerStopAll(); telegramStart(); }
      updateWifiPage(); gotoPage(3);
      break;
    case MI_SHUFFLE:
      playerSetShuffle(!gSettings.shuffle); updateMenuTexts(); updateIcons();
      break;
    case MI_BRIGHT:
      brightEdit = true; updateMenuFocus();
      break;
    case MI_BT:
      gSettings.bluetooth = !gSettings.bluetooth; settingsDirty(); updateMenuTexts();
      uiToast("Bluetooth setting saved.\nBluetooth audio is not\nsupported yet.");
      break;
    case MI_SETTINGS:
      gotoPage(2);
      break;
  }
}

static void menuEvent(const BtnEvent& e) {
  bool press = (e.type == BE_PRESS), rep = (e.type == BE_REPEAT);
  if (!press && !rep) return;
  if (e.id == BTN_VOLUP) { changeVolume(VOLUME_STEP); return; }
  if (e.id == BTN_VOLDN) { changeVolume(-VOLUME_STEP); return; }
  if (brightEdit) {
    if (e.id == BTN_UP)   changeBrightness(BRIGHT_STEP_PCT);
    else if (e.id == BTN_DOWN) changeBrightness(-BRIGHT_STEP_PCT);
    else if (press && (e.id == BTN_BACK || e.id == BTN_PLAY || e.id == BTN_HOME)) { brightEdit = false; updateMenuFocus(); }
    return;
  }
  if (menuLevel == 1) {
    if (e.id == BTN_UP)   { menuFocus = (menuFocus + 4) % 5; updateMenuFocus(); }
    else if (e.id == BTN_DOWN) { menuFocus = (menuFocus + 1) % 5; updateMenuFocus(); }
    else if (press && e.id == BTN_PLAY) activateItem();
    else if (press && (e.id == BTN_BACK || e.id == BTN_HOME)) closeMenu();
  } else if (menuLevel == 2) {
    if (e.id == BTN_UP)   { themeFocus = (themeFocus + THEME_N - 1) % THEME_N; updateMenuFocus(); }
    else if (e.id == BTN_DOWN) { themeFocus = (themeFocus + 1) % THEME_N; updateMenuFocus(); }
    else if (press && e.id == BTN_PLAY) { gSettings.theme = themeFocus; settingsDirty(); applyTheme(themeFocus); updateMenuFocus(); updateMenuTexts(); }
    else if (press && e.id == BTN_BACK) gotoPage(1);
    else if (press && e.id == BTN_HOME) closeMenu();
  } else if (menuLevel == 3) {
    if (press && e.id == BTN_PLAY) { telegramStop(); gotoPage(1); }
    else if (press && e.id == BTN_BACK) { if (telegramStatus().phase == TG_ERROR) telegramStop(); gotoPage(1); }
    else if (press && e.id == BTN_HOME) { if (telegramStatus().phase == TG_ERROR) telegramStop(); closeMenu(); }
  }
}

static void screenEvent(const BtnEvent& e) {
  bool press = (e.type == BE_PRESS), rep = (e.type == BE_REPEAT);
  uint32_t now = millis();
  if (press && e.id == BTN_HOME) { openMenu(); return; }
  if (press && e.id == BTN_BACK) { showScreen(!onList); return; }
  if ((press || rep) && e.id == BTN_VOLUP) { changeVolume(VOLUME_STEP); return; }
  if ((press || rep) && e.id == BTN_VOLDN) { changeVolume(-VOLUME_STEP); return; }

  if (!onList) {                                           // ---- Now Playing
    if (press && e.id == BTN_PLAY) { if (playAllowed()) playerToggle(); return; }
    if (e.id == BTN_UP || e.id == BTN_DOWN) {
      int k = (e.id == BTN_UP) ? 0 : 1;
      if (rep && now - lastSeekMs >= SEEK_REPEAT_MS) { lastSeekMs = now; playerSeekRel(k == 0 ? SEEK_STEP_MS : -SEEK_STEP_MS); }
      if (e.type == BE_RELEASE) {
        if (e.heldMs < BTN_REPEAT_DELAY_MS) {
          if (tapCnt[k] > 0 && now - tapAt[k] <= TAP_GAP_MS) tapCnt[k]++; else tapCnt[k] = 1;
          tapAt[k] = now;
          if (tapCnt[k] >= 3) { tapCnt[k] = 0; if (playAllowed()) { if (k == 0) playerNext(); else playerPrev(); } }
        } else tapCnt[k] = 0;
      }
    }
  } else {                                                  // ---- PlayList
    int n = libraryCount();
    if ((press || rep) && e.id == BTN_UP   && n) { if (sel > 0)     { sel--; refreshRows(); scrollToSel(true); } }
    if ((press || rep) && e.id == BTN_DOWN && n) { if (sel < n - 1) { sel++; refreshRows(); scrollToSel(true); } }
    if (press && e.id == BTN_PLAY && n) {
      if (!playAllowed()) return;
      if (sel == playerCurrent() && playerState() != PS_STOPPED) playerToggle(); else playerPlayIndex(sel);
      refreshRows(); updateIcons();
    }
  }
}

// ================================================================== LOOP ====
void uiLoop() {
  uint32_t now = millis();
  buttonsPoll();

  Song ns;
  bool added = false;
  while (telegramTakeSong(ns)) { if (libraryAppend(ns)) { added = true; } }
  if (added) { playerLibraryChanged(); uiLibraryChanged(); }

  BtnEvent e;
  while (buttonsGet(e)) {
    uint8_t bit = 1 << e.id;
    if (!displayIsOn()) {
      if (e.type == BE_PRESS) { displayOn(); swallowMask |= bit; lastInputMs = now; }
      continue;
    }
    if (swallowMask & bit) { if (e.type == BE_RELEASE) swallowMask &= ~bit; continue; }
    lastInputMs = now;
    if (menuLevel) menuEvent(e); else screenEvent(e);
  }
  if (displayIsOn() && now - lastInputMs > SCREEN_OFF_MS) displayOff();

  playerLoop();

  static uint32_t lastTick = 0;
  static PlayerState lastPs = PS_STOPPED;
  if (now - lastTick >= 120) {
    lastTick = now;
    if (playerTakeChanged()) { updateNow(); refreshRows(); }
    if (playerState() != lastPs) { lastPs = playerState(); refreshRows(); }
    updateNowTime(); updateIcons();
    if (volHideAt && now > volHideAt) { volHideAt = 0; lv_obj_add_flag(volPill, LV_OBJ_FLAG_HIDDEN); }
    if (toastHideAt && now > toastHideAt) { toastHideAt = 0; lv_obj_add_flag(toastBox, LV_OBJ_FLAG_HIDDEN); }
    if (menuLevel) { updateMenuTexts(); if (menuLevel == 3) updateWifiPage(); }
  }
  if (displayIsOn()) displayLoop();
}
