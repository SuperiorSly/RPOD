#include "sdcard.h"
#include "config.h"
#include <SPI.h>
#include <lvgl.h>

static SPIClass sdSpi(VSPI);
static SemaphoreHandle_t sdMutex = nullptr;
static bool sdOk = false;

void sdLock()   { if (sdMutex) xSemaphoreTakeRecursive(sdMutex, portMAX_DELAY); }
void sdUnlock() { if (sdMutex) xSemaphoreGiveRecursive(sdMutex); }
bool sdReady()  { return sdOk; }

bool sdBegin() {
  if (!sdMutex) sdMutex = xSemaphoreCreateRecursiveMutex();
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  SdGuard g;
  sdOk = SD.begin(PIN_SD_CS, sdSpi, 20000000);
  if (!sdOk) sdOk = SD.begin(PIN_SD_CS, sdSpi, 10000000);   // slower retry
  if (sdOk && SD.cardType() == CARD_NONE) sdOk = false;
  if (sdOk) {
    if (!SD.exists(DIR_MUSIC))  SD.mkdir(DIR_MUSIC);
    if (!SD.exists(DIR_DATA))   SD.mkdir(DIR_DATA);
    if (!SD.exists(DIR_COVERS)) SD.mkdir(DIR_COVERS);
    if (!SD.exists(DIR_THUMBS)) SD.mkdir(DIR_THUMBS);
  }
  return sdOk;
}

uint64_t sdFreeBytes() {
  if (!sdOk) return 0;
  SdGuard g;
  return SD.totalBytes() - SD.usedBytes();
}
uint64_t sdTotalBytes() {
  if (!sdOk) return 0;
  SdGuard g;
  return SD.totalBytes();
}

// ------------------------------------------------ LVGL file-system driver ---
static void* fsOpen(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
  if (mode != LV_FS_MODE_RD || !sdOk) return nullptr;
  SdGuard g;
  File f = SD.open(path, FILE_READ);
  if (!f) return nullptr;
  return new File(f);
}
static lv_fs_res_t fsClose(lv_fs_drv_t*, void* fp) {
  SdGuard g;
  File* f = (File*)fp;
  f->close();
  delete f;
  return LV_FS_RES_OK;
}
static lv_fs_res_t fsRead(lv_fs_drv_t*, void* fp, void* buf, uint32_t btr, uint32_t* br) {
  SdGuard g;
  *br = ((File*)fp)->read((uint8_t*)buf, btr);
  return LV_FS_RES_OK;
}
static lv_fs_res_t fsSeek(lv_fs_drv_t*, void* fp, uint32_t pos, lv_fs_whence_t whence) {
  SdGuard g;
  File* f = (File*)fp;
  SeekMode m = (whence == LV_FS_SEEK_CUR) ? SeekCur : (whence == LV_FS_SEEK_END) ? SeekEnd : SeekSet;
  return f->seek(pos, m) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}
static lv_fs_res_t fsTell(lv_fs_drv_t*, void* fp, uint32_t* pos) {
  SdGuard g;
  *pos = ((File*)fp)->position();
  return LV_FS_RES_OK;
}

void sdRegisterLvgl() {
  static lv_fs_drv_t drv;
  lv_fs_drv_init(&drv);
  drv.letter   = 'S';
  drv.open_cb  = fsOpen;
  drv.close_cb = fsClose;
  drv.read_cb  = fsRead;
  drv.seek_cb  = fsSeek;
  drv.tell_cb  = fsTell;
  lv_fs_drv_register(&drv);
}
