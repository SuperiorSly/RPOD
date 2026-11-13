#pragma once
// ============================================================
//  PLAYER — SD playlist, ID3 (title/artist/duration/cover),
//  cover BMP cache on SD, MP3 decode -> built-in DAC GPIO26
//
//  Audio facts proven in Phase 1 on THIS unit:
//   * the board amp never outputs -> sound leaves via the R4
//     solder wire connected to GPIO26 (= DAC2 of the ESP32)
//   * built-in DAC consumes the MSB byte of each 16-bit slot
//   * GPIO25 is XPT2046 CLK here -> enable ONLY DAC2 (GPIO26)
//     and write the same sample into both slots (either slot map)
// ============================================================
#include <Arduino.h>
#include <SD.h>
#include <driver/i2s.h>
#include <AudioOutput.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceSD.h>
#include <tjpgd.h>

// ---------------- SD access guard (SD lib is NOT thread-safe) ----------------
static SemaphoreHandle_t sdMutex = nullptr;

struct SDLock {
  SDLock()  { if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY); }
  ~SDLock() { if (sdMutex) xSemaphoreGive(sdMutex); }
};

// Audio file source with the same lock around every SD read (the audio task
// reads while the UI / Telegram tasks may also touch SD).
class AudioFileSourceSDCYD : public AudioFileSourceSD {
public:
  AudioFileSourceSDCYD(const char* p) : AudioFileSourceSD(p) {}
  uint32_t read(void* data, uint32_t length) override {
    if (!sdMutex) return AudioFileSourceSD::read(data, length);
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    uint32_t r = AudioFileSourceSD::read(data, length);
    xSemaphoreGive(sdMutex);
    return r;
  }
  bool seek(int32_t pos, int dir) override {
    if (!sdMutex) return AudioFileSourceSD::seek(pos, dir);
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    bool r = AudioFileSourceSD::seek(pos, dir);
    xSemaphoreGive(sdMutex);
    return r;
  }
};

// ---------------- playlist store ----------------
static const int MAX_TRACKS = 96;
struct Track {
  char     file[46];
  char     title[56];
  char     artist[40];
  uint32_t durMs;
  uint32_t apicOff;    // 0 = no cover; file offset of the JPEG bytes
  uint32_t apicLen;
};
static Track g_tracks[MAX_TRACKS];
static int   g_trackCount = 0;
static int   g_curTrack   = -1;   // index currently loaded (-1 = none)

// ---------------- cover cache: raw RGB565 images on SD ----------------
//  /covercache/<stem>.big  = 160x160 u16   (Now Playing art)
//  /covercache/<stem>.th   = 60x60  u16    (playlist rows)
static const int CVR_W = 160, TH_W = 60;
static const char CACHE_DIR[] = "/covercache";

static void coverCacheName(const Track& t, bool big, char* out, size_t n)
{
  const char* base = strrchr(t.file, '/'); base = base ? base + 1 : t.file;
  size_t bl = strlen(base);
  const char* dot = strrchr(base, '.');
  int stemLen = dot ? (int)(dot - base) : (int)bl;
  if (stemLen <= 0) stemLen = (int)bl;
  snprintf(out, n, "%s/%.*s%s", CACHE_DIR, stemLen, base, big ? ".big" : ".th");
}

// Draw a cached RGB565 image. Returns false when absent/wrong size.
static bool drawCachedCover(const char* path, int x, int y, int wantW, int wantH)
{
  if (!sdMutex || !SD.exists(path)) return false;
  File f;
  { SDLock l; f = SD.open(path, FILE_READ); }
  if (!f) return false;
  bool ok = false;
  uint8_t hdr[8];
  if (f.read(hdr, 8) == 8 && hdr[0] == 'C' && hdr[1] == 'B' &&
      (int)(hdr[2] | (hdr[3] << 8)) == wantW && (int)(hdr[4] | (hdr[5] << 8)) == wantH) {
    ok = true;
  }
  if (!ok) { SDLock l; f.close(); return false; }

  static uint16_t chunk[CVR_W * 40];                 // 12.8 KB, UI-task only
  for (int yr = 0; yr < wantH; yr += 40) {
    int h = min(40, wantH - yr);
    uint32_t need = (uint32_t)wantW * h * 2;
    { SDLock l;
      uint32_t got = f.read((uint8_t*)chunk, need);
      (void)got;
    }
    tft.pushImage(x, y + yr, wantW, h, chunk);
  }
  { SDLock l; f.close(); }
  return true;
}

// ---------------- JPEG -> sprite (tjpgd core, streaming from SD) ----------------
struct JCtx { File f; uint32_t off, len, got; };
static LGFX_Sprite* jpgSp    = nullptr;   // target square sprite
static int          jpgIW    = 1, jpgIH  = 1;    // decoded size (after scale)

static size_t jpgIn(JDEC* jd, uint8_t* dest, size_t nd)
{
  (void)jd;
  JCtx* c = (JCtx*)jd->device;
  if (!c || c->got >= c->len) return 0;
  size_t take = (nd > (size_t)(c->len - c->got)) ? (size_t)(c->len - c->got) : nd;
  SDLock l;
  int r = c->f.read(dest, take);
  if (r <= 0) return 0;
  c->got += r;
  return (size_t)r;
}

static int jpgOut(JDEC* jd, void* bitmap, JRECT* rect)
{
  (void)jd;
  if (!jpgSp) return 1;
  const int T = jpgSp->width();
  const int rw = rect->right - rect->left + 1;
  uint8_t* src = (uint8_t*)bitmap;                  // 3 bytes/px, B,G,R order
  for (int rr = rect->top; rr <= rect->bottom; rr++) {
    int ty = rr * T / jpgIH;
    if (ty < 0 || ty >= T) continue;
    int prevTx = -1;
    uint8_t* row = src + (rr - rect->top) * rw * 3;
    for (int xx = rect->left; xx <= rect->right; xx++) {
      int tx = xx * T / jpgIW;
      if (tx >= 0 && tx < T && tx != prevTx) {
        prevTx = tx;
        const uint8_t* px = row + (xx - rect->left) * 3;
        uint32_t r = px[2], g = px[1], b = px[0];
        uint16_t c565 = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        jpgSp->drawPixel(tx, ty, c565);
      }
    }
  }
  return 1;
}

// Decode cover JPEG embedded in t (range apicOff..+apicLen) into a
// TARGETxTARGET sprite, cache it on SD, return true on success.
static bool decodeCoverJpeg(Track& t, bool big)
{
  if (!t.apicOff) return false;
  const int target = big ? CVR_W : TH_W;
  char cpath[64]; coverCacheName(t, big, cpath, sizeof(cpath));
  if (drawCachedCover(cpath, 0, 0, target, target)) return true;   // cache hit

  File f;
  { SDLock l; f = SD.open(t.file, FILE_READ); }
  if (!f) return false;

  JCtx  ctx = { f, t.apicOff, t.apicLen, 0 };
  ctx.f.seek(t.apicOff);
  JDEC jd; uint8_t work[3100];
  JRESULT rc = jd_prepare(&jd, jpgIn, work, sizeof(work), &ctx);
  if (rc != JDR_OK) { SDLock l; f.close(); return false; }

  jpgIW = jd.width; jpgIH = jd.height;
  int scale = 0;                                    // 0..3 = /1 /2 /4 /8
  while ((jpgIW >> (scale + 1)) >= target && scale < 3) scale++;

  LGFX_Sprite sp(&tft);
  sp.setColorDepth(16);
  if (!sp.createSprite(target, target)) { SDLock l; f.close(); return false; }
  sp.fillScreen(C_CARD);
  jpgSp = &sp;
  rc = jd_decomp(&jd, jpgOut, scale);
  jpgSp = nullptr;

  if (rc == JDR_OK) {
    { SDLock l;
      File o = SD.open(cpath, FILE_WRITE);
      if (o) {
        uint8_t hdr[8] = { 'C','B',(uint8_t)(target & 0xFF),(uint8_t)(target >> 8),
                           (uint8_t)(target & 0xFF),(uint8_t)(target >> 8),0,0 };
        o.write(hdr, 8);
        uint8_t* base = (uint8_t*)sp.getBuffer();          // 2 bytes per pixel!
        for (int y = 0; y < target; y++)
          o.write(base + (size_t)y * target * 2, target * 2);
        o.close();
      }
    }
  }
  { SDLock l; f.close(); }
  return (rc == JDR_OK);
}

// ---------------- live playback counters ----------------
// (written by the audio task / output; read everywhere)
static volatile long     g_samplesPlayed = 0;
static volatile uint32_t g_elapsedMs     = 0;

// ---------------- I2S built-in DAC output (GPIO26) ----------------
class AudioOutputCYD : public AudioOutput {
public:
  int  myRate   = 44100;
  bool muted    = false;
  bool i2sReady = false;

  bool installI2S() {
    if (i2sReady) return true;
    i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
      .sample_rate = 44100,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,                    // underrun = silence
      .fixed_mclk = 0
    };
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);      // DAC2 = GPIO26 only
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2sReady = true;
    return true;
  }

  bool SetRate(int hz) override {
    myRate = hz;
    if (i2sReady) i2s_set_clk(I2S_NUM_0, hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    return true;
  }
  bool SetChannels(int)      override { return true; }   // base has no SetBitsPerSample

  bool begin() override {
    if (!installI2S()) return false;
    i2s_start(I2S_NUM_0);
    i2s_set_clk(I2S_NUM_0, myRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    return true;
  }
  bool stop() override { i2s_zero_dma_buffer(I2S_NUM_0); return true; }

  bool ConsumeSample(int16_t s[2]) override {
    int32_t mono = ((int32_t)s[0] + (int32_t)s[1]) / 2;
    float   v    = (mono / 32768.0f) * (g_volume / 20.0f);
    int val = muted ? 128 : constrain(128 + (int)(v * 127.0f), 0, 255);
    uint16_t slot = (uint16_t)(val << 8);
    uint16_t frame[2] = { slot, slot };
    size_t wr;
    i2s_write(I2S_NUM_0, frame, 4, &wr, portMAX_DELAY);   // blocking = pacing
    g_samplesPlayed++;
    if ((g_samplesPlayed & 0x1FF) == 0) {
      g_elapsedMs = (uint32_t)((int64_t)g_samplesPlayed * 1000 / myRate);
    }
    return true;
  }
};

// ---------------- decoder + audio task ----------------
static AudioGeneratorMP3*    g_mp3 = nullptr;
static AudioOutputCYD*       g_out = nullptr;
static AudioFileSourceSDCYD* g_src = nullptr;

static volatile bool     g_eof           = false;
static volatile bool     g_playing       = false;
static volatile bool     g_paused        = false;
static volatile int      g_seekTargetMs  = -1;
static volatile bool     g_uiTrackDirty  = false;   // UI redraws track info

static volatile int  g_cmdPlayIdx   = -1;
static volatile bool g_cmdPauseLoad = false;     // load but don't start
static volatile bool g_cmdToggle    = false;
static volatile bool g_cmdNext      = false;
static volatile bool g_cmdPrev      = false;
static volatile int32_t g_cmdSeekAbs = -1;       // UI asks absolute ms position

// live state snaps for the UI
struct PlayerState {
  bool     playing, paused;
  uint32_t elapsedMs;
  int      curIdx;
};
static void playerSnapshot(PlayerState& ps)
{
  ps.playing  = g_playing && !g_paused;
  ps.paused   = g_playing && g_paused;
  ps.elapsedMs= g_elapsedMs;
  ps.curIdx   = g_curTrack;
}

static int nextIdx(bool prev)
{
  if (g_trackCount <= 0) return 0;
  int n;
  if (g_shuffle) n = random(g_trackCount);
  else n = prev ? (g_curTrack - 1 + g_trackCount) % g_trackCount
                : (g_curTrack + 1) % g_trackCount;
  return n;
}

static void audioTask(void*)
{
  for (;;) {
    if (g_cmdPlayIdx >= 0 && g_trackCount) {
      int idx = g_cmdPlayIdx; g_cmdPlayIdx = -1;
      bool pauseLoad = g_cmdPauseLoad; g_cmdPauseLoad = false;
      if (g_mp3 && g_mp3->isRunning()) g_mp3->stop();
      if (g_mp3) { delete g_mp3; g_mp3 = nullptr; }
      if (g_src) { delete g_src; g_src = nullptr; }
      g_curTrack = idx;
      g_samplesPlayed = 0; g_elapsedMs = 0; g_eof = false; g_seekTargetMs = -1;
      { SDLock l;
        g_src = new AudioFileSourceSDCYD(g_tracks[idx].file); }
      g_mp3 = new AudioGeneratorMP3();
      if (g_out) g_out->muted = false;            // never carry mute across tracks
      g_mp3->begin(g_src, g_out);
      g_playing = true;
      g_paused  = pauseLoad;
      g_uiTrackDirty = true;
    }
    if (g_cmdToggle) {
      g_cmdToggle = false;
      if (g_playing) g_paused = !g_paused;
    }
    if (g_cmdNext || g_cmdPrev || g_eof) {
      bool wantPrev = g_cmdPrev && !g_eof;
      g_cmdNext = false; g_cmdPrev = false;
      int n = wantPrev ? nextIdx(true) : nextIdx(false);
      g_eof = false;
      g_cmdPlayIdx = n;                             // audio task starts next loop
      g_cmdPauseLoad = false;
      continue;
    }
    if (g_cmdSeekAbs >= 0) {
      int target = g_cmdSeekAbs; g_cmdSeekAbs = -1;
      if (g_playing && g_curTrack >= 0 && g_mp3) {
        // Rewind-support: reopen the file from byte 0 with decoder only,
        // then fast-decode (muted) to the target position.
        bool wasPaused = g_paused;
        if (g_mp3->isRunning()) g_mp3->stop();
        delete g_mp3; g_mp3 = nullptr;
        delete g_src; g_src = nullptr;
        g_samplesPlayed = 0; g_elapsedMs = 0;
        { SDLock l;
          g_src = new AudioFileSourceSDCYD(g_tracks[g_curTrack].file); }
        g_mp3 = new AudioGeneratorMP3();
        g_mp3->begin(g_src, g_out);
        g_playing = true;
        g_paused  = wasPaused;
        g_seekTargetMs = target;      // consumed by the decode loop below
      }
    }

    if (g_mp3 && g_src && g_playing) {
      if (g_paused) {
        if (g_out) i2s_zero_dma_buffer(I2S_NUM_0);
        vTaskDelay(pdMS_TO_TICKS(40));
      } else if (g_seekTargetMs > (int)g_elapsedMs) {
        g_out->muted = true;                        // fast-decode silently
        g_mp3->loop();
        if (g_elapsedMs >= (uint32_t)g_seekTargetMs) { g_seekTargetMs = -1; g_out->muted = false; }
        if (!g_mp3->isRunning()) { g_eof = true; g_seekTargetMs = -1; g_out->muted = false; }
      } else {
        if (!g_mp3->loop()) { g_eof = true; g_playing = false; }
        if (g_seekTargetMs < 0 && g_out->muted) g_out->muted = false;
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

// ---------------- ID3 scan ----------------
static const uint16_t BR_L3_M1[15] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
static const uint16_t BR_L3_M2[15] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};

static void utfToAscii(uint8_t enc, uint8_t* d, int len, char* out, int n)
{
  int o = 0, i = 0;
  if (enc == 1 && len >= 2 && ((d[0] == 0xFF && d[1] == 0xFE) || (d[0] == 0xFE && d[1] == 0xFF))) i = 2;
  bool be = (enc == 2) || (enc == 1 && len >= 2 && d[0] == 0xFE && d[1] == 0xFF);
  for (; i < len && o < n - 1; ) {
    uint32_t c;
    if (enc == 1 || enc == 2) {
      if (i + 2 > len) break;
      uint16_t w = be ? ((d[i] << 8) | d[i + 1]) : (d[i] | (d[i + 1] << 8));
      i += 2;
      if (w == 0) break;
      c = (w < 0x80) ? w : '?';
    } else if (enc == 3 && (d[i] & 0x80)) {
      c = '?'; i++; while (i < len && (d[i] & 0xC0) == 0x80) i++;
    } else {
      c = d[i++];
      if (c == 0) break;
    }
    if (c < 0x20) c = ' ';
    out[o++] = (char)c;
  }
  out[o] = 0;
}

static void scanTrack(const char* path)
{
  if (g_trackCount >= MAX_TRACKS) return;
  Track& t = g_tracks[g_trackCount];
  memset(&t, 0, sizeof(t));
  strncpy(t.file, path, sizeof(t.file) - 1);
  strcpy(t.artist, "Unknown Artist");
  t.title[0] = 0;

  File f;
  { SDLock l; f = SD.open(path, FILE_READ); }
  if (!f) return;
  uint32_t fsize = f.size();

  uint8_t h[10];
  if (f.read(h, 10) != 10) { SDLock l; f.close(); return; }
  uint32_t tagEnd = 0;
  if (h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
    bool v4 = (h[3] == 0x04);                      // v4 sizes are syncsafe; v2/v3 plain 32-bit
    uint32_t tagSize = v4
      ? (((uint32_t)(h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) | ((h[8] & 0x7F) << 7) | (h[9] & 0x7F))
      : (((uint32_t)h[6] << 24) | ((uint32_t)h[7] << 16) | ((uint32_t)h[8] << 8) | h[9]);
    tagEnd = 10 + tagSize;
    bool haveT = false, haveA = false;
    uint32_t pos = 10;
    while (pos + 10 <= tagEnd) {
      uint8_t fh[10];
      if (!f.seek(pos + 0) || f.read(fh, 10) != 10) break;
      if (fh[0] < 'A' || fh[0] > 'Z') break;                 // padding/garbage
      uint32_t fsz = v4
        ? (((uint32_t)(fh[4] & 0x7F) << 21) | ((fh[5] & 0x7F) << 14) | ((fh[6] & 0x7F) << 7) | (fh[7] & 0x7F))
        : (((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) | ((uint32_t)fh[6] << 8) | fh[7]);
      if (fsz == 0 || (uint64_t)pos + 10 + fsz > fsize) break;

      uint8_t body[512];
      uint32_t rd = min(fsz, (uint32_t)sizeof(body) - 1);
      f.seek(pos + 10);
      if (f.read(body, rd) != rd) break;
      uint32_t dataOff = pos + 10;

      if (!memcmp(fh, "TIT2", 4) && !haveT && rd > 1) {
        utfToAscii(body[0], body + 1, rd - 1, t.title, sizeof(t.title));
        haveT = true;
      } else if (!memcmp(fh, "TPE1", 4) && !haveA && rd > 1) {
        utfToAscii(body[0], body + 1, rd - 1, t.artist, sizeof(t.artist));
        haveA = true;
      } else if (!memcmp(fh, "TLEN", 4) && t.durMs == 0 && rd > 1) {
        static char digits[24];
        int ln = 0;
        for (uint32_t q = 1; q < rd && ln < 23; q++) if (isdigit(body[q])) digits[ln++] = body[q];
        digits[ln] = 0;
        long ms = atol(digits);
        if (ms > 0 && ms < 3600000L) t.durMs = (uint32_t)ms;
      } else if (!memcmp(fh, "APIC", 4) && t.apicOff == 0) {
        uint32_t q = 1;
        while (q < rd && body[q]) q++;                       // mime
        q++;                                                 // mime NUL
        q++;                                                 // picture type
        if (body[0] == 1 || body[0] == 2) {                  // utf16 description
          for (; q + 1 < rd; q += 2) if (body[q] == 0 && body[q + 1] == 0) break;
          q += 2;
        } else {                                             // latin1/utf8 description
          while (q < rd && body[q]) q++;
          q++;
        }
        if (q + 4 <= rd && body[q] == 0xFF && body[q + 1] == 0xD8) {   // JPEG SOI
          t.apicOff = dataOff + q;
          t.apicLen  = fsz - q;
        }
      }
      if (haveT && haveA && t.durMs && t.apicOff) break;
      pos += 10 + fsz;
    }
  }

  // ---- duration estimate from the first MP3 frame header ----
  if (t.durMs == 0) {
    static uint8_t w[8192];
    f.seek(tagEnd < fsize ? tagEnd : 0);
    uint32_t got = f.read(w, sizeof(w));
    for (uint32_t i = 0; i + 4 < got; i++) {
      if (w[i] == 0xFF && (w[i + 1] & 0xE0) == 0xE0 && (w[i + 1] & 0x08)) {   // not padding
        int ver  = (w[i + 1] & 0x18) >> 3;
        int kbps = (w[i + 2] & 0xF0) >> 4;
        if (kbps > 0 && kbps < 15) {
          int k = (ver == 3) ? BR_L3_M1[kbps] : BR_L3_M2[kbps];
          if (k) { t.durMs = (uint32_t)(((uint64_t)(fsize - tagEnd) * 8000) / (k * 1000)); break; }
        }
      }
    }
  }
  { SDLock l; f.close(); }
  g_trackCount++;
}

static void titleFallback(Track& t)
{
  if (t.title[0]) return;
  const char* base = strrchr(t.file, '/'); base = base ? base + 1 : t.file;
  int n = 0;
  for (const char* p = base; *p && *p != '.'; p++) if (n < (int)sizeof(t.title) - 1) t.title[n++] = *p;
  t.title[n] = 0;
}

static void playerScan()
{
  g_trackCount = 0;
  { SDLock l;
    if (!SD.exists("/music")) SD.mkdir("/music");
    if (!SD.exists("/covercache")) SD.mkdir("/covercache");
  }
  File root;
  { SDLock l; root = SD.open("/music"); }
  if (!root) return;
  static char paths[MAX_TRACKS][46];
  int n = 0;
  for (File f = root.openNextFile(); f && n < MAX_TRACKS; f = root.openNextFile()) {
    if (!f.isDirectory()) {
      const char* nm = f.name();
      size_t ln = strlen(nm);
      if (ln > 4 && !strcasecmp(nm + ln - 4, ".mp3")) {
        snprintf(paths[n], sizeof(paths[n]), "/music/%s", nm);
        n++;
      }
    }
  }
  { SDLock l; root.close(); }
  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (strcasecmp(paths[j], paths[i]) < 0) { char tmp[46]; memcpy(tmp, paths[i], 46); memcpy(paths[i], paths[j], 46); memcpy(paths[j], tmp, 46); }
  for (int i = 0; i < n; i++) { scanTrack(paths[i]); titleFallback(g_tracks[i]); }
}

static void playerInit()
{
  sdMutex = xSemaphoreCreateMutex();
  { SDLock l;
    if (!SD.begin(5)) {                      // VSPI: CS5, SCK18, MISO19, MOSI23
      Serial.println("[player] SD mount FAILED");
    }
  }
  playerScan();
  g_out = new AudioOutputCYD();
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 5, nullptr, 0);
}

// ---------------- UI-facing controls ----------------
static void playIdx(int idx, bool autoPlay = true)
{
  if (g_trackCount == 0) return;
  idx = constrain(idx, 0, g_trackCount - 1);
  g_lastIdx = idx; prefsSave();
  g_cmdPauseLoad = !autoPlay;
  g_cmdPlayIdx = idx;
}
static void playerToggle() { g_cmdToggle = true; }
static void playerNext()   { g_cmdNext = true; }
static void playerPrev()   { g_cmdPrev = true; }
static void playerSeekTo(int32_t ms)
{
  if (g_trackCount == 0 || g_curTrack < 0) return;
  Track& t = g_tracks[g_curTrack];
  if (ms < 0) ms = 0;
  if (t.durMs && ms > (int32_t)t.durMs) ms = (int32_t)t.durMs;  // no clamp when duration unknown
  g_cmdSeekAbs = ms;
}
static void playerSeekBy(int32_t dms)
{
  int t = (int)g_elapsedMs + dms;
  playerSeekTo(max(0, t));
}
