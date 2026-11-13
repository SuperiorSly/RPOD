#include "library.h"
#include "config.h"
#include "sdcard.h"
#include <SD.h>
#include <TJpg_Decoder.h>
#include <lvgl.h>

static Song*   songs = nullptr;
static int     songCount = 0;

void libraryInit() {
  if (!songs) songs = (Song*)calloc(MAX_SONGS, sizeof(Song));
  songCount = 0;
}
int         libraryCount() { return songCount; }
const Song& librarySong(int i) { return songs[i]; }
int libraryFindById(uint32_t id) {
  for (int i = 0; i < songCount; i++) if (songs[i].id == id) return i;
  return -1;
}

void fmtTime(uint32_t sec, char* out, size_t n) {
  snprintf(out, n, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

void libraryCoverPath(const Song& s, char* out, size_t n, bool thumb) {
  snprintf(out, n, "S:%s/%08x.bin", thumb ? DIR_THUMBS : DIR_COVERS, (unsigned)s.id);
}
static void coverFile(const Song& s, char* out, size_t n, bool thumb) {   // no "S:"
  snprintf(out, n, "%s/%08x.bin", thumb ? DIR_THUMBS : DIR_COVERS, (unsigned)s.id);
}

// ------------------------------------------------------- tiny SD helpers ---
static size_t rd(File& f, void* b, size_t n) { SdGuard g; return f.read((uint8_t*)b, n); }
static bool   sk(File& f, uint32_t p)        { SdGuard g; return f.seek(p); }

// ------------------------------------------------------- text conversion ---
static const char LAT1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
static const char LATA[] =
  "AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh" "IiIiIiIiIi" "Ii" "Jj" "Kkk"
  "LlLlLlLlLl" "NnNnNnnNn" "OoOoOo" "Oo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu"
  "Ww" "YyY" "ZzZzZz" "s";

static void putCp(uint32_t c, char* out, int& o, int max, int& bad) {
  const char* rep = nullptr; char one[2] = {0, 0};
  if (c >= 0x20 && c < 0x7F) { one[0] = (char)c; rep = one; }
  else if (c >= 0xC0 && c <= 0xFF) { one[0] = LAT1[c - 0xC0]; rep = one; }
  else if (c >= 0x100 && c <= 0x17F) { one[0] = LATA[c - 0x100]; rep = one; }
  else if (c == 0x2018 || c == 0x2019 || c == 0x60 || c == 0xB4) rep = "'";
  else if (c == 0x201C || c == 0x201D) rep = "\"";
  else if (c == 0x2013 || c == 0x2014) rep = "-";
  else if (c == 0x2026) rep = "...";
  else if (c == 0xA0 || c == 0x09) rep = " ";
  else if (c == 0 || c < 0x20) rep = nullptr;
  else { rep = "?"; bad++; }
  if (!rep) return;
  while (*rep && o < max - 1) out[o++] = *rep++;
}

// enc: 0 Latin-1, 1 UTF-16 (BOM), 2 UTF-16BE, 3 UTF-8
static void decodeText(const uint8_t* d, int n, uint8_t enc, char* out, int max) {
  int o = 0, bad = 0, total = 0;
  if (enc == 0) {
    for (int i = 0; i < n && d[i]; i++) { putCp(d[i], out, o, max, bad); total++; }
  } else if (enc == 3) {
    for (int i = 0; i < n && d[i];) {
      uint32_t c = d[i];
      int len = 1;
      if (c >= 0xF0)      { c &= 0x07; len = 4; }
      else if (c >= 0xE0) { c &= 0x0F; len = 3; }
      else if (c >= 0xC0) { c &= 0x1F; len = 2; }
      for (int k = 1; k < len && i + k < n; k++) c = (c << 6) | (d[i + k] & 0x3F);
      i += len; total++;
      putCp(c, out, o, max, bad);
    }
  } else {
    bool be = (enc == 2); int i = 0;
    if (enc == 1 && n >= 2) {
      if (d[0] == 0xFE && d[1] == 0xFF) { be = true; i = 2; }
      else if (d[0] == 0xFF && d[1] == 0xFE) { be = false; i = 2; }
    }
    for (; i + 1 < n; i += 2) {
      uint32_t c = be ? ((d[i] << 8) | d[i + 1]) : ((d[i + 1] << 8) | d[i]);
      if (c == 0) break;
      total++;
      if (c >= 0xD800 && c < 0xE000) { putCp(0x3F00, out, o, max, bad); i += 2; continue; }
      putCp(c, out, o, max, bad);
    }
  }
  out[o] = 0;
  while (o > 0 && out[o - 1] == ' ') out[--o] = 0;
  if (total > 0 && bad * 2 > total) out[0] = 0;       // mostly unsupported glyphs
}

// ----------------------------------------------------------- MPEG header ---
static const uint16_t BR_V1L1[] = {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448};
static const uint16_t BR_V1L2[] = {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384};
static const uint16_t BR_V1L3[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
static const uint16_t BR_V2L1[] = {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256};
static const uint16_t BR_V2L23[]= {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
static const uint32_t SR_TAB[3][3] = {{11025,12000,8000},{0,0,0},{22050,24000,16000}};

struct MpegInfo { bool ok; uint32_t sr; uint32_t kbps; int frameLen; int spf; bool v1; int layer; bool mono; };

static bool parseHdr(const uint8_t* h, MpegInfo& m) {
  if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
  int ver = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3;
  int bri = (h[2] >> 4) & 15, sri = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
  if (ver == 1 || layer == 0 || bri == 0 || bri == 15 || sri == 3) return false;
  m.v1 = (ver == 3);
  m.layer = 4 - layer;                                   // 1,2,3
  uint32_t sr = m.v1 ? (sri == 0 ? 44100 : sri == 1 ? 48000 : 32000) : SR_TAB[ver == 2 ? 2 : 0][sri];
  const uint16_t* t;
  if (m.v1) t = (m.layer == 1) ? BR_V1L1 : (m.layer == 2) ? BR_V1L2 : BR_V1L3;
  else      t = (m.layer == 1) ? BR_V2L1 : BR_V2L23;
  m.kbps = t[bri]; m.sr = sr;
  m.mono = ((h[3] >> 6) & 3) == 3;
  if (m.layer == 1) { m.spf = 384; m.frameLen = (12 * m.kbps * 1000 / sr + pad) * 4; }
  else if (m.layer == 2) { m.spf = 1152; m.frameLen = 144 * m.kbps * 1000 / sr + pad; }
  else { m.spf = m.v1 ? 1152 : 576; m.frameLen = (m.v1 ? 144 : 72) * m.kbps * 1000 / sr + pad; }
  m.ok = m.frameLen > 4;
  return m.ok;
}

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static uint32_t mp3Duration(File& f, uint32_t audioStart, uint32_t fileSize) {
  uint8_t* b = (uint8_t*)malloc(4096);
  if (!b) return 0;
  uint32_t dur = 0;
  sk(f, audioStart);
  int n = rd(f, b, 4096);
  for (int i = 0; i + 4 < n; i++) {
    MpegInfo m;
    if (!parseHdr(b + i, m)) continue;
    int nx = i + m.frameLen;
    if (nx + 4 < n) { MpegInfo m2; if (!parseHdr(b + nx, m2)) continue; }   // confirm sync
    int side = m.v1 ? (m.mono ? 17 : 32) : (m.mono ? 9 : 17);
    uint32_t frames = 0;
    int x = i + 4 + side;
    if (x + 12 < n && (!memcmp(b + x, "Xing", 4) || !memcmp(b + x, "Info", 4))) {
      if (b[x + 7] & 1) frames = be32(b + x + 8);
    } else if (i + 4 + 32 + 18 < n && !memcmp(b + i + 36, "VBRI", 4)) {
      frames = be32(b + i + 36 + 14);
    }
    if (frames) dur = (uint32_t)((uint64_t)frames * m.spf / m.sr);
    else if (m.kbps) {
      uint32_t bytes = fileSize > audioStart ? fileSize - audioStart : 0;
      if (bytes > 128) bytes -= 128;
      dur = (uint32_t)((uint64_t)bytes * 8 / (m.kbps * 1000));
    }
    break;
  }
  free(b);
  return dur;
}

// -------------------------------------------------------------- covers -----
#define COV_ROWS 88                      // cover is decoded in 2 strips to save RAM
struct CovCtx {
  File*     f;
  uint16_t *cov, *thm;
  int       rowBase;                     // first cover row held in 'cov' (this pass)
  uint16_t  mapXc[COVER_PX], mapYc[COVER_PX], mapXt[THUMB_PX], mapYt[THUMB_PX];
};

static size_t jdIn(JDEC* jd, uint8_t* buf, size_t n) {
  CovCtx* c = (CovCtx*)jd->device;
  SdGuard g;
  if (buf) return c->f->read(buf, n);
  uint32_t p = c->f->position();
  return c->f->seek(p + n) ? n : 0;
}

static inline uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }   // LV_COLOR_16_SWAP=1

// copy the part of a decoded block that lands on destination rows [r0, r0+rows)
static void blit(uint16_t* dst, int D, int r0, int rows, const uint16_t* mx, const uint16_t* my, const uint16_t* src, JRECT* r) {
  int bw = r->right - r->left + 1;
  for (int dy = r0; dy < r0 + rows; dy++) {
    int sy = my[dy];
    if (sy < r->top || sy > r->bottom) continue;
    const uint16_t* row = src + (sy - r->top) * bw - r->left;
    uint16_t* o = dst + (dy - r0) * D;
    for (int dx = 0; dx < D; dx++) {
      int sx = mx[dx];
      if (sx >= r->left && sx <= r->right) o[dx] = sw16(row[sx]);
    }
  }
}

static int jdOut(JDEC* jd, void* bmp, JRECT* r) {
  CovCtx* c = (CovCtx*)jd->device;
  blit(c->cov, COVER_PX, c->rowBase, COV_ROWS, c->mapXc, c->mapYc, (uint16_t*)bmp, r);
  if (c->thm) blit(c->thm, THUMB_PX, 0, THUMB_PX, c->mapXt, c->mapYt, (uint16_t*)bmp, r);
  return 1;
}

static bool writeChunk(File& o, const uint16_t* px, size_t bytes) {
  const uint8_t* p = (const uint8_t*)px;
  while (bytes) {
    size_t n = bytes > 2048 ? 2048 : bytes;
    SdGuard g;
    if (o.write(p, n) != n) return false;
    p += n; bytes -= n;
  }
  return true;
}

static bool writeHeader(File& o, int w, int h) {
  lv_img_header_t hd; memset(&hd, 0, sizeof(hd));
  hd.cf = LV_IMG_CF_TRUE_COLOR; hd.w = w; hd.h = h;
  SdGuard g;
  return o.write((const uint8_t*)&hd, 4) == 4;
}

static bool makeCovers(Song& s, const char* jpgPath) {
  File f;
  { SdGuard g; f = SD.open(jpgPath, FILE_READ); }
  if (!f) return false;
  CovCtx* c = (CovCtx*)calloc(1, sizeof(CovCtx));
  uint8_t* work = (uint8_t*)malloc(4400);
  bool ok = false;
  char p1[48], p2[48];
  coverFile(s, p1, sizeof p1, false); coverFile(s, p2, sizeof p2, true);
  File fc, ft;
  if (c && work) {
    c->f = &f;
    c->thm = (uint16_t*)calloc(THUMB_PX * THUMB_PX, 2);
    c->cov = (uint16_t*)calloc(COVER_PX * COV_ROWS, 2);
    { SdGuard g; SD.remove(p1); SD.remove(p2); fc = SD.open(p1, FILE_WRITE); ft = SD.open(p2, FILE_WRITE); }
    if (c->thm && c->cov && fc && ft && writeHeader(fc, COVER_PX, COVER_PX) && writeHeader(ft, THUMB_PX, THUMB_PX)) {
      ok = true;
      for (int pass = 0; pass < 2 && ok; pass++) {
        { SdGuard g; f.seek(0); }
        JDEC jd;
        if (jd_prepare(&jd, jdIn, work, 4400, c) != JDR_OK) { ok = false; break; }
        int mn = jd.width < jd.height ? jd.width : jd.height;
        int sc = 0;
        while (sc < 3 && (mn >> (sc + 1)) >= COVER_PX) sc++;
        int sw = jd.width >> sc, sh = jd.height >> sc;
        int cs = sw < sh ? sw : sh, cx = (sw - cs) / 2, cy = (sh - cs) / 2;
        for (int d = 0; d < COVER_PX; d++) { c->mapXc[d] = cx + d * cs / COVER_PX; c->mapYc[d] = cy + d * cs / COVER_PX; }
        for (int d = 0; d < THUMB_PX; d++) { c->mapXt[d] = cx + d * cs / THUMB_PX; c->mapYt[d] = cy + d * cs / THUMB_PX; }
        c->rowBase = pass * COV_ROWS;
        memset(c->cov, 0, COVER_PX * COV_ROWS * 2);
        if (jd_decomp(&jd, jdOut, sc) != JDR_OK) { ok = false; break; }
        ok = writeChunk(fc, c->cov, COVER_PX * COV_ROWS * 2);
        if (ok && pass == 0) ok = writeChunk(ft, c->thm, THUMB_PX * THUMB_PX * 2);
      }
    }
    { SdGuard g; if (fc) fc.close(); if (ft) ft.close(); if (!ok) { SD.remove(p1); SD.remove(p2); } }
    if (c->thm) free(c->thm);
    if (c->cov) free(c->cov);
  }
  if (c) free(c);
  if (work) free(work);
  { SdGuard g; f.close(); }
  return ok;
}

// ----------------------------------------------------------------- ID3 -----
static uint32_t syncsafe(const uint8_t* p) { return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F); }

static void baseName(const char* path, char* out, size_t n) {
  const char* b = strrchr(path, '/'); b = b ? b + 1 : path;
  strncpy(out, b, n - 1); out[n - 1] = 0;
  char* dot = strrchr(out, '.'); if (dot) *dot = 0;
  for (char* q = out; *q; q++) if (*q == '_') *q = ' ';
}

bool libraryProcessFile(const char* path, Song& s) {
  memset(&s, 0, sizeof(s));
  strncpy(s.path, path, sizeof(s.path) - 1);
  File f;
  { SdGuard g; f = SD.open(path, FILE_READ); }
  if (!f) return false;
  uint32_t fileSize; { SdGuard g; fileSize = f.size(); }
  {
    const char* nm = strrchr(path, '/'); nm = nm ? nm + 1 : path;
    uint32_t h = 2166136261u;
    for (const char* q = nm; *q; q++) { h ^= (uint8_t)tolower(*q); h *= 16777619u; }
    s.id = h ^ (fileSize * 2654435761u);
    if (s.id == 0) s.id = 1;
  }

  uint8_t hd[10];
  uint32_t audioStart = 0;
  bool haveCover = false;
  char tTitle[40] = "", tArtist[32] = "", tAlbArtist[32] = "";
  uint8_t* fb = (uint8_t*)malloc(160);

  sk(f, 0);
  if (fb && rd(f, hd, 10) == 10 && !memcmp(hd, "ID3", 3) && hd[3] >= 2 && hd[3] <= 4) {
    int ver = hd[3];
    uint8_t flags = hd[5];
    uint32_t tagSize = syncsafe(hd + 6);
    uint32_t pos = 10, end = 10 + tagSize;
    audioStart = end + ((flags & 0x10) ? 10 : 0);
    bool tagUnsync = flags & 0x80;
    if (ver >= 3 && (flags & 0x40)) {                 // extended header: skip it
      uint8_t eh[4]; sk(f, pos);
      if (rd(f, eh, 4) == 4) pos += (ver == 4) ? syncsafe(eh) : (be32(eh) + 4);
    }
    int hl = (ver == 2) ? 6 : 10;
    while (pos + hl <= end && pos + hl < fileSize) {
      uint8_t fh[10]; sk(f, pos);
      if (rd(f, fh, hl) != (size_t)hl || fh[0] == 0) break;
      char id[5] = {0}; memcpy(id, fh, ver == 2 ? 3 : 4);
      uint32_t sz; uint16_t fl = 0;
      if (ver == 2)      sz = (fh[3] << 16) | (fh[4] << 8) | fh[5];
      else if (ver == 3) { sz = be32(fh + 4); fl = (fh[8] << 8) | fh[9]; }
      else               { sz = syncsafe(fh + 4); fl = (fh[8] << 8) | fh[9]; }
      uint32_t dpos = pos + hl;
      pos = dpos + sz;
      if (sz == 0 || pos > end) { if (pos > end) break; continue; }

      bool compressed = (ver == 3) ? (fl & 0x00C0) : (ver == 4) ? (fl & 0x000C) : false;
      bool unsync     = tagUnsync || ((ver == 4) && (fl & 0x0002));
      uint32_t skipDli = (ver == 4 && (fl & 0x0001)) ? 4 : 0;
      if (compressed) continue;

      bool isTitle  = !strcmp(id, "TIT2") || !strcmp(id, "TT2");
      bool isArtist = !strcmp(id, "TPE1") || !strcmp(id, "TP1");
      bool isAlbArt = !strcmp(id, "TPE2") || !strcmp(id, "TP2");
      bool isPic    = !strcmp(id, "APIC") || !strcmp(id, "PIC");

      if (isTitle || isArtist || isAlbArt) {
        uint32_t n = sz - skipDli; if (n > 150) n = 150;
        if (n < 2) continue;
        sk(f, dpos + skipDli);
        size_t got = rd(f, fb, n);
        if (got < 2) continue;
        char* dst = isTitle ? tTitle : isArtist ? tArtist : tAlbArtist;
        int mx = isTitle ? sizeof(tTitle) : sizeof(tArtist);
        decodeText(fb + 1, got - 1, fb[0], dst, mx);
      } else if (isPic && !haveCover && !unsync && sz > 200 && sz < 3000000UL) {
        uint32_t p = dpos + skipDli, pe = dpos + sz;
        uint8_t ph[8]; sk(f, p);
        if (rd(f, ph, 1) != 1) continue;
        uint8_t enc = ph[0]; p++;
        if (ver == 2) p += 3;                                   // "JPG"/"PNG"
        else {                                                 // mime string, null-terminated
          char mime[24]; int mi = 0; sk(f, p);
          while (mi < 23) { uint8_t ch; if (rd(f, &ch, 1) != 1) break; p++; if (!ch) break; mime[mi++] = ch; }
          mime[mi] = 0;
        }
        p++;                                                   // picture type
        sk(f, p);                                              // description
        if (enc == 1 || enc == 2) { uint8_t pr[2]; while (rd(f, pr, 2) == 2) { p += 2; if (!pr[0] && !pr[1]) break; if (p > pe) break; } }
        else { uint8_t ch; while (rd(f, &ch, 1) == 1) { p++; if (!ch) break; if (p > pe) break; } }
        if (p >= pe) continue;
        // copy image bytes to a temp file, check JPEG magic
        uint8_t magic[2]; sk(f, p);
        if (rd(f, magic, 2) != 2 || magic[0] != 0xFF || magic[1] != 0xD8) continue;   // PNG etc. not supported
        bool ok = true;
        { SdGuard g; SD.remove(FILE_TMPJPG); }
        File t; { SdGuard g; t = SD.open(FILE_TMPJPG, FILE_WRITE); }
        if (!t) continue;
        uint8_t* cb = (uint8_t*)malloc(1024);
        if (cb) {
          uint32_t left = pe - p; sk(f, p);
          while (left && ok) {
            size_t n = left > 1024 ? 1024 : left;
            size_t g2 = rd(f, cb, n);
            if (g2 == 0) { ok = false; break; }
            SdGuard g; ok = t.write(cb, g2) == g2;
            left -= g2;
          }
          free(cb);
        } else ok = false;
        { SdGuard g; t.close(); }
        if (ok) haveCover = makeCovers(s, FILE_TMPJPG);
        { SdGuard g; SD.remove(FILE_TMPJPG); }
      }
    }
  }

  // ID3v1 fallback
  if (!tTitle[0] && fileSize > 128) {
    uint8_t v1[128]; sk(f, fileSize - 128);
    if (rd(f, v1, 128) == 128 && !memcmp(v1, "TAG", 3)) {
      decodeText(v1 + 3, 30, 0, tTitle, sizeof(tTitle));
      if (!tArtist[0]) decodeText(v1 + 33, 30, 0, tArtist, sizeof(tArtist));
    }
  }
  if (!tArtist[0] && tAlbArtist[0]) strcpy(tArtist, tAlbArtist);
  if (!tTitle[0]) baseName(path, tTitle, sizeof(tTitle));
  if (!tArtist[0]) strcpy(tArtist, "Unknown Artist");
  strncpy(s.title, tTitle, sizeof(s.title) - 1);
  strncpy(s.artist, tArtist, sizeof(s.artist) - 1);
  s.durSec = (uint16_t)min<uint32_t>(mp3Duration(f, audioStart, fileSize), 65535);
  s.hasCover = haveCover ? 1 : 0;
  if (fb) free(fb);
  { SdGuard g; f.close(); }
  return true;
}

// -------------------------------------------------------------- index ------
struct IdxHdr { uint32_t magic; uint32_t ver; uint32_t count; uint32_t recSize; };
#define IDX_MAGIC 0x49504F44u

void librarySaveIndex() {
  SdGuard g;
  SD.remove(FILE_INDEX);
  File o = SD.open(FILE_INDEX, FILE_WRITE);
  if (!o) return;
  IdxHdr h = { IDX_MAGIC, 2, (uint32_t)songCount, sizeof(Song) };
  o.write((const uint8_t*)&h, sizeof h);
  o.write((const uint8_t*)songs, sizeof(Song) * songCount);
  o.close();
}

static Song* oldIdx = nullptr; static int oldCount = 0;
static void loadOldIndex() {
  oldCount = 0;
  SdGuard g;
  File f = SD.open(FILE_INDEX, FILE_READ);
  if (!f) return;
  IdxHdr h;
  if (f.read((uint8_t*)&h, sizeof h) == sizeof h && h.magic == IDX_MAGIC && h.ver == 2 &&
      h.recSize == sizeof(Song) && h.count <= MAX_SONGS) {
    oldIdx = (Song*)malloc(sizeof(Song) * (h.count ? h.count : 1));
    if (oldIdx && f.read((uint8_t*)oldIdx, sizeof(Song) * h.count) == sizeof(Song) * h.count) oldCount = h.count;
  }
  f.close();
}

static bool isMp3Name(const char* n) {
  if (n[0] == '.') return false;
  size_t l = strlen(n);
  return l > 4 && !strcasecmp(n + l - 4, ".mp3");
}

bool libraryAppend(const Song& s) {
  if (songCount >= MAX_SONGS || libraryFindById(s.id) >= 0) return false;
  songs[songCount++] = s;
  librarySaveIndex();
  return true;
}

void libraryScan(ScanProgressCb cb) {
  songCount = 0;
  if (!sdReady()) return;
  loadOldIndex();

  // 1. list the files
  char (*names)[80] = (char(*)[80])malloc(80 * MAX_SONGS);
  uint32_t* sizes = (uint32_t*)malloc(4 * MAX_SONGS);
  int nn = 0;
  if (names && sizes) {
    SdGuard g;
    File dir = SD.open(DIR_MUSIC);
    if (dir) {
      for (File e = dir.openNextFile(); e && nn < MAX_SONGS; e = dir.openNextFile()) {
        if (!e.isDirectory() && isMp3Name(e.name()) && strlen(e.name()) + strlen(DIR_MUSIC) + 2 < 80) {
          snprintf(names[nn], 80, "%s/%s", DIR_MUSIC, e.name());
          sizes[nn] = e.size();
          nn++;
        }
        e.close();
      }
      dir.close();
    }
  }

  // 2. keep known songs (in their saved order), then add new ones alphabetically
  bool* used = (bool*)calloc(MAX_SONGS, 1);
  bool changed = (oldCount == 0);
  for (int i = 0; i < oldCount && used; i++) {
    for (int k = 0; k < nn; k++) {
      if (!used[k] && !strcmp(oldIdx[i].path, names[k])) {
        // same name: check size through the id
        const char* nm = strrchr(names[k], '/') + 1;
        uint32_t h = 2166136261u;
        for (const char* q = nm; *q; q++) { h ^= (uint8_t)tolower(*q); h *= 16777619u; }
        if ((h ^ (sizes[k] * 2654435761u)) == oldIdx[i].id) { songs[songCount++] = oldIdx[i]; used[k] = true; }
        break;
      }
    }
  }
  if (songCount != oldCount) changed = true;

  // sort the remaining (new) names
  int order[MAX_SONGS], no = 0;
  for (int k = 0; k < nn; k++) if (!used || !used[k]) order[no++] = k;
  for (int a = 1; a < no; a++) { int v = order[a], b = a - 1; while (b >= 0 && strcasecmp(names[order[b]], names[v]) > 0) { order[b + 1] = order[b]; b--; } order[b + 1] = v; }

  for (int i = 0; i < no && songCount < MAX_SONGS; i++) {
    const char* p = names[order[i]];
    if (cb) cb(i, no, strrchr(p, '/') + 1);
    Song s;
    if (libraryProcessFile(p, s)) { songs[songCount++] = s; changed = true; }
  }
  if (cb) cb(no, no, "");
  if (changed) librarySaveIndex();

  if (oldIdx) { free(oldIdx); oldIdx = nullptr; }
  if (names) free(names);
  if (sizes) free(sizes);
  if (used) free(used);
}
