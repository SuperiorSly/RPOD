// MP3 decoding (libhelix) -> ESP32 built-in DAC2 (GPIO26) through I2S, on its own task.
#include "audio.h"
#include "config.h"
#include "sdcard.h"
#include <SD.h>
#include <driver/i2s.h>
#include <AudioGeneratorMP3a.h>   // pulls in the ESP8266Audio library (we only use its libhelix decoder)
#include <libhelix-mp3/mp3dec.h>

#define I2S_PORT      I2S_NUM_0
#define BUFSZ         2048
#define BLOCK_FRAMES  256

enum CmdType : uint8_t { C_PLAY, C_PAUSE, C_RESUME, C_STOP, C_SEEK, C_VOL, C_TEST };
struct Cmd { CmdType t; uint32_t a; uint32_t b; char path[80]; };

static QueueHandle_t   cmdQ = nullptr;
static volatile uint8_t  gState = AS_IDLE;
static volatile uint32_t gPosMs = 0;
static volatile bool     gEnded = false;

// ---- everything below is touched only by the audio task -------------------
static HMP3Decoder dec = nullptr;
static uint8_t*    ibuf = nullptr;
static int16_t*    pcm = nullptr;
static File        file;
static bool        fileOpen = false;
static int         valid = 0;
static bool        srcEof = false;
static uint32_t    audioStart = 0, audioBytes = 0, durMs = 0, baseMs = 0;
static uint64_t    framesPlayed = 0;
static uint32_t    rate = 44100;
static uint8_t     volPct = AUDIO_DEFAULT_VOLUME;
static uint32_t    gainTgt = 0, gainCur = 0;       // Q16
static int32_t     lastOut = 0;
static uint32_t    out32[BLOCK_FRAMES];
static int         outN = 0;
static bool        pausing = false;
static uint8_t     testMode = 0;
static uint32_t    sinePhase = 0;
static uint32_t    rnd = 12345;

static uint32_t volToGain(uint8_t v) {
  float g = (float)v / 100.0f;
  g = g * (AUDIO_MAX_GAIN_PCT / 100.0f);      // linear: an 8-bit DAC needs the level
  return (uint32_t)(g * 65536.0f);
}

static void i2sWrite(const uint32_t* d, int frames) {
  size_t wr;
  i2s_write(I2S_PORT, d, frames * 4, &wr, portMAX_DELAY);
}
static inline uint32_t dacWord(int32_t s) {              // s16 -> unsigned, both words equal
  int32_t u = s + 32768;
  if (u < 0) u = 0; if (u > 65535) u = 65535;
  return ((uint32_t)u << 16) | (uint32_t)u;
}
static void flushOut() { if (outN) { i2sWrite(out32, outN); outN = 0; } }
static void pushWord(uint32_t w) { out32[outN++] = w; if (outN == BLOCK_FRAMES) flushOut(); }

static void setRate(uint32_t hz) {
  if (hz == rate || hz < 8000 || hz > 48000) return;
  flushOut();
  rate = hz;
  i2s_set_sample_rates(I2S_PORT, hz);
}

static void freeDecoder() {
  if (dec) { MP3FreeDecoder(dec); dec = nullptr; }
  if (ibuf) { free(ibuf); ibuf = nullptr; }
  if (pcm)  { free(pcm);  pcm = nullptr; }
  if (fileOpen) { SdGuard g; file.close(); fileOpen = false; }
}

// short ramp to the middle level so stopping never clicks
static void softLanding() {
  flushOut();
  int32_t v = lastOut;
  for (int i = 0; i < BLOCK_FRAMES; i++) { out32[i] = dacWord(v * (BLOCK_FRAMES - 1 - i) / (BLOCK_FRAMES - 1)); }
  i2sWrite(out32, BLOCK_FRAMES);
  lastOut = 0; gainCur = 0;
}

static void idleBlock() {
  uint32_t w = dacWord(0);
  for (int i = 0; i < BLOCK_FRAMES; i++) {
    if (testMode == 1) {
      sinePhase += (uint32_t)(440.0 * 4294967296.0 / rate);
      float s = sinf(sinePhase * (6.2831853f / 4294967296.0f));
      int32_t v = (int32_t)(s * 22000.0f * ((float)gainTgt / 65536.0f)) ;
      out32[i] = dacWord(v);
    } else if (testMode == 2) out32[i] = 0x00000000;
    else if (testMode == 3) out32[i] = 0x80008000;
    else if (testMode == 4) out32[i] = 0xFF00FF00;
    else out32[i] = w;
  }
  i2sWrite(out32, BLOCK_FRAMES);
}

static bool startTrack(const char* path, uint32_t startMs, uint32_t durSec) {
  if (gState == AS_PLAYING || gState == AS_PAUSED) softLanding();
  freeDecoder();
  gState = AS_IDLE; pausing = false;
  { SdGuard g; file = SD.open(path, FILE_READ); fileOpen = (bool)file; }
  if (!fileOpen) return false;
  uint32_t size; { SdGuard g; size = file.size(); }
  // skip the ID3 tag
  uint8_t h[10]; audioStart = 0;
  { SdGuard g;
    if (file.read(h, 10) == 10 && !memcmp(h, "ID3", 3))
      audioStart = 10 + (((h[6] & 0x7F) << 21) | ((h[7] & 0x7F) << 14) | ((h[8] & 0x7F) << 7) | (h[9] & 0x7F)) + ((h[5] & 0x10) ? 10 : 0);
  }
  audioBytes = size > audioStart + 128 ? size - audioStart - 128 : 0;
  durMs = durSec * 1000UL;
  dec  = MP3InitDecoder();
  ibuf = (uint8_t*)malloc(BUFSZ);
  pcm  = (int16_t*)malloc(1152 * 2 * sizeof(int16_t));
  if (!dec || !ibuf || !pcm) { freeDecoder(); return false; }
  uint32_t pos = audioStart;
  if (startMs && durMs) {
    if (startMs >= durMs) startMs = durMs > 1000 ? durMs - 1000 : 0;
    pos += (uint32_t)((uint64_t)startMs * audioBytes / durMs);
  } else startMs = 0;
  { SdGuard g; file.seek(pos); }
  valid = 0; srcEof = false; baseMs = startMs; framesPlayed = 0; gPosMs = startMs;
  gainCur = 0; gainTgt = volToGain(volPct); lastOut = 0;
  gState = AS_PLAYING;
  return true;
}

// 1 = frame decoded into pcm, 0 = end of file
static int decodeFrame(int& nFrames, int& nCh) {
  for (int tries = 0; tries < 200; tries++) {
    if (valid < 1700 && !srcEof) {
      int n; { SdGuard g; n = file.read(ibuf + valid, BUFSZ - valid); }
      if (n <= 0) srcEof = true; else valid += n;
    }
    if (valid < 4) return 0;
    int off = MP3FindSyncWord(ibuf, valid);
    if (off < 0) {
      int keep = valid < 3 ? valid : 3;
      memmove(ibuf, ibuf + valid - keep, keep); valid = keep;
      if (srcEof) return 0;
      continue;
    }
    if (off > 0) { memmove(ibuf, ibuf + off, valid - off); valid -= off; }
    unsigned char* p = ibuf; int left = valid;
    int err = MP3Decode(dec, &p, &left, pcm, 0);
    int used = valid - left;
    if (err == 0) {
      memmove(ibuf, ibuf + used, left); valid = left;
      MP3FrameInfo fi; MP3GetLastFrameInfo(dec, &fi);
      if (fi.samprate) setRate(fi.samprate);
      nCh = fi.nChans < 1 ? 1 : fi.nChans;
      nFrames = fi.outputSamps / nCh;
      if (nFrames <= 0) continue;
      return 1;
    }
    if (err == ERR_MP3_INDATA_UNDERFLOW && valid < BUFSZ) { if (srcEof) return 0; continue; }
    if (used < 1) used = 1;
    if (used > valid) used = valid;
    memmove(ibuf, ibuf + used, valid - used); valid -= used;
  }
  return 0;
}

static void handleCmd(const Cmd& c) {
  switch (c.t) {
    case C_PLAY:
      if (!startTrack(c.path, c.a, c.b)) { gEnded = true; gState = AS_IDLE; }
      break;
    case C_PAUSE:
      if (gState == AS_PLAYING) { pausing = true; gainTgt = 0; }
      break;
    case C_RESUME:
      if (gState == AS_PAUSED || pausing) { pausing = false; gainTgt = volToGain(volPct); gState = AS_PLAYING; }
      break;
    case C_STOP:
      if (gState != AS_IDLE) softLanding();
      freeDecoder(); gState = AS_IDLE; pausing = false;
      break;
    case C_SEEK:
      if ((gState == AS_PLAYING || gState == AS_PAUSED) && durMs) {
        uint32_t ms = c.a; if (ms >= durMs) ms = durMs > 800 ? durMs - 800 : 0;
        uint32_t pos = audioStart + (uint32_t)((uint64_t)ms * audioBytes / durMs);
        { SdGuard g; file.seek(pos); }
        valid = 0; srcEof = false;
        if (dec) { MP3FreeDecoder(dec); dec = MP3InitDecoder(); }
        baseMs = ms; framesPlayed = 0; gPosMs = ms;
        gainCur = 0;                                         // quick fade-in
      }
      break;
    case C_VOL:
      volPct = c.a > 100 ? 100 : c.a;
      if (!pausing && gState != AS_PAUSED) gainTgt = volToGain(volPct);
      break;
    case C_TEST:
      testMode = c.a;
      if (testMode) { gainTgt = volToGain(volPct < 60 ? 60 : volPct); }
      break;
  }
}

static void audioTask(void*) {
  Cmd c;
  for (;;) {
    while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) handleCmd(c);

    if (gState == AS_PLAYING && dec) {
      int nF = 0, nCh = 1;
      if (!decodeFrame(nF, nCh)) {               // end of song
        softLanding(); freeDecoder(); gState = AS_IDLE; gEnded = true;
        continue;
      }
      uint32_t step = (uint32_t)(65536000UL / (rate * 12UL)) + 1;       // ~12 ms full-scale slew
      for (int i = 0; i < nF; i++) {
        int32_t s = (nCh == 1) ? pcm[i] : ((int32_t)pcm[2 * i] + pcm[2 * i + 1]) >> 1;
        if (gainCur < gainTgt) { gainCur += step; if (gainCur > gainTgt) gainCur = gainTgt; }
        else if (gainCur > gainTgt) { gainCur = (gainCur > step + gainTgt) ? gainCur - step : gainTgt; }
        int32_t v = (int32_t)(((int64_t)s * gainCur) >> 16);
#if AUDIO_DITHER
        rnd = rnd * 1664525u + 1013904223u; v += (int32_t)((rnd >> 24) & 0xFF) - 128;
#endif
        lastOut = v;
        pushWord(dacWord(v));
        framesPlayed++;
        if (pausing && gainCur == 0) {           // fully faded out -> really pause
          flushOut(); pausing = false; gState = AS_PAUSED; break;
        }
      }
      gPosMs = baseMs + (uint32_t)(framesPlayed * 1000ULL / rate);
    } else {
      flushOut();
      idleBlock();                               // keeps the DAC parked at the middle level
      if (gState == AS_IDLE && !testMode) { /* nothing */ }
    }
  }
}

// ----------------------------------------------------------------- API -----
static void post(CmdType t, uint32_t a = 0, uint32_t b = 0, const char* p = nullptr) {
  if (!cmdQ) return;
  Cmd c; memset(&c, 0, sizeof c);
  c.t = t; c.a = a; c.b = b;
  if (p) strncpy(c.path, p, sizeof(c.path) - 1);
  xQueueSend(cmdQ, &c, pdMS_TO_TICKS(50));
}

void audioInit() {
  cmdQ = xQueueCreate(10, sizeof(Cmd));
  dacWrite(PIN_DAC_AUDIO, 128);                 // park the output at 1.65 V first
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = 44100;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, NULL);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  i2s_set_sample_rates(I2S_PORT, 44100);
  gainTgt = volToGain(volPct);
  xTaskCreatePinnedToCore(audioTask, "audio", 10240, nullptr, 4, nullptr, 0);
}

void audioPlay(const char* path, uint32_t startMs, uint32_t durSec) { gEnded = false; post(C_PLAY, startMs, durSec, path); }
void audioPause()  { post(C_PAUSE); }
void audioResume() { post(C_RESUME); }
void audioStop()   { post(C_STOP); }
void audioSeekMs(uint32_t ms) { post(C_SEEK, ms); }
void audioSetVolume(uint8_t pct) { post(C_VOL, pct); }
void audioTestMode(uint8_t m) { post(C_TEST, m); }
AudioState audioState() { return (AudioState)gState; }
uint32_t audioPosMs() { return gPosMs; }
bool audioTakeEnded() { bool e = gEnded; gEnded = false; return e; }
