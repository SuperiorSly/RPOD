// Telegram bot worker: receives MP3 files, saves them to /music, reads tags + cover.
#include "telegram.h"
#include "config.h"
#include "secrets.h"
#include "sdcard.h"
#include "shot.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>   // kept for reference (download flow may use its stream type)
#include <HTTPClient.h>
#include "esp_heap_caps.h"   // TODO: remove after WiFi debugging (heap probe)
#include <ArduinoJson.h>
#include <SD.h>

static portMUX_TYPE     mux = portMUX_INITIALIZER_UNLOCKED;
static TgStatus         st;
static volatile bool    stopReq = false;
static volatile bool    alive = false;
static QueueHandle_t    songQ = nullptr;
static int64_t          offsetId = 0;

static void setStatus(TgPhase p, const char* line = nullptr, int pct = -1) {
  portENTER_CRITICAL(&mux);
  st.phase = p;
  if (line) { strncpy(st.line, line, sizeof(st.line) - 1); st.line[sizeof(st.line) - 1] = 0; }
  if (pct >= 0) st.pct = pct;
  portEXIT_CRITICAL(&mux);
}

TgStatus telegramStatus() { portENTER_CRITICAL(&mux); TgStatus c = st; portEXIT_CRITICAL(&mux); return c; }
bool telegramActive() { return alive; }
bool telegramTakeSong(Song& out) { return songQ && xQueueReceive(songQ, &out, 0) == pdTRUE; }

// ------------------------------------------------------------ HTTP ---------
static String urlEnc(const String& s) {
  String o; o.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

// --- relay discovery ---------------------------------------------------------
// The PC's IP can drift (DHCP). At every boot the bot broadcasts "IPIPOD1?" on
// UDP 8461; tg_relay.ps1 answers "IPIPOD1!" and the reply's sender address is
// the live relay. Falls back to TG_RELAY_HOST if nothing answers.
static String   relayHost = TG_RELAY_HOST;      // "a.b.c.d:8461" used by every call
static bool relayResolve() {
  uint32_t t0 = millis();
  WiFiUDP udp;
  if (!udp.begin(8462)) { Serial.println("[tg] DISC udp begin failed"); return false; }
  bool found = false;
  for (int round = 0; round < 4 && !found; round++) {
    udp.beginPacket(IPAddress(255, 255, 255, 255), 8461);
    udp.print("IPIPOD1?");
    udp.endPacket();
    uint32_t tw = millis();
    while (millis() - tw < 700) {
      int n = udp.parsePacket();
      if (n > 0) {
        char rb[12] = {0};
        udp.read(rb, sizeof rb - 1);
        if (!strncmp(rb, "IPIPOD1!", 8)) {
          relayHost = udp.remoteIP().toString() + ":8461";
          Serial.printf("[tg] DISC relay at %s (in %u ms)\n", relayHost.c_str(), (unsigned)(millis() - t0));
          found = true;
        }
      } else vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
  udp.stop();
  return found;
}

static String apiUrl(const char* method) { return String("http://") + relayHost + "/" + method; }   // hobby relay: no TLS on the device

static bool httpGetString(const String& url, String& body, int timeoutMs = 9000) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setReuse(false);
  if (!http.begin(url)) { Serial.println("[tg] http begin failed"); return false; }
  int code = http.GET();
  bool ok = (code == 200);
  if (!ok) {
    Serial.printf("[tg] http GET failed, code=%d (%s)\n", code, http.errorToString(code).c_str());
  }
  if (ok) body = http.getString();
  http.end();
  return ok;
}

static void sendText(int64_t chat, const String& text) {
  String u = apiUrl("sendMessage") + "?chat_id=" + String((long long)chat) + "&text=" + urlEnc(text);
  String b; httpGetString(u, b, 6000);
}

// ---------------------------------------------------------- file names -----
static void safeName(const char* in, char* out, size_t n) {
  size_t o = 0;
  for (; *in && o < n - 5; in++) {
    char c = *in;
    if (isalnum((uint8_t)c) || c == '-' || c == '_' || c == '.' || c == '(' || c == ')') out[o++] = c;
    else if (c == ' ') out[o++] = '_';
  }
  out[o] = 0;
  size_t l = strlen(out);
  if (l < 4 || strcasecmp(out + l - 4, ".mp3")) { if (l == 0) strcpy(out, "song"); strcat(out, ".mp3"); }
  while (out[0] == '.') memmove(out, out + 1, strlen(out));
}

// -------------------------------------------------------------- download ---
static bool dlOnce(const String& url, uint32_t expect, const char* tmpPath) {
  HTTPClient http;
  http.setTimeout(30000);          // header wait (12 s was too short while the relay fetched from Telegram)
  http.setReuse(false);
  if (!http.begin(url)) { Serial.println("[tg] DL begin failed"); return false; }
  int code = http.GET();
  if (code != 200) { Serial.printf("[tg] DL GET failed, code=%d (%s)\n", code, http.errorToString(code).c_str()); http.end(); return false; }
  int total = http.getSize();
  if (total <= 0) total = (int)expect;
  WiFiClient* s = http.getStreamPtr();
  uint8_t* buf = (uint8_t*)malloc(4096);
  if (!buf) { Serial.println("[tg] DL out of memory (4 KB buffer)"); http.end(); return false; }
  File f;
  { SdGuard g; SD.remove(tmpPath); f = SD.open(tmpPath, FILE_WRITE); }
  if (!f) { Serial.println("[tg] DL: could not create temp file on SD"); free(buf); http.end(); return false; }
  uint32_t got = 0, fill = 0, lastData = millis();
  int lastPct = -1;
  bool ok = true;
  while (!stopReq) {
    int av = s->available();
    if (av > 0) {
      int n = s->read(buf + fill, min<int>(av, 4096 - fill));
      if (n > 0) { fill += n; got += n; lastData = millis(); }
      if (fill == 4096) {
        SdGuard g; if (f.write(buf, fill) != fill) { ok = false; break; }
        fill = 0;
      }
      int pct = total > 0 ? (int)((uint64_t)got * 100 / total) : 0;
      if (pct != lastPct) { lastPct = pct; setStatus(TG_DOWNLOADING, nullptr, pct); }
      if (total > 0 && got >= (uint32_t)total) break;
    } else {
      if (!s->connected() && !s->available()) break;
      if (millis() - lastData > 20000) { Serial.printf("[tg] DL stalled: no data for 20 s (got=%u)\n", got); ok = false; break; }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  int we = f.getWriteError();
  if (ok && fill) { SdGuard g; ok = (f.write(buf, fill) == fill); if (!ok) we = f.getWriteError(); }
  { SdGuard g; f.close(); }
  free(buf);
  http.end();
  if (stopReq) { Serial.printf("[tg] DL aborted by user, got=%u\n", got); ok = false; }
  else if (ok && total > 0 && got < (uint32_t)total) { Serial.printf("[tg] DL short read: got %u of %d\n", got, total); ok = false; }
  if (!ok) { SdGuard g; SD.remove(tmpPath); Serial.printf("[tg] DL FAILED: got=%u total=%d sdWriteErr=%d\n", got, total, we); }
  else     { Serial.printf("[tg] DL complete: %u bytes\n", got); }
  return ok;
}

static bool downloadFile(const String& url, uint32_t expect, const char* tmpPath) {
  for (int attempt = 1; attempt <= 2; attempt++) {   // one automatic retry: absorbs one-off stalls
    if (dlOnce(url, expect, tmpPath)) return true;
    if (stopReq || attempt == 2) return false;
    Serial.println("[tg] DL retrying once in 1 s...");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  return false;
}

// ----------------------------------------------------------- message -------
static void handleFile(int64_t chat, const char* fileId, const char* fname, uint32_t fsize,
                       const char* title, const char* performer, long msgId) {
  if (fsize > TG_MAX_FILE_BYTES) { sendText(chat, "That file is bigger than 20 MB, the Telegram bot limit. Send a smaller MP3."); return; }
  uint64_t freeB = sdFreeBytes();
  if (freeB < (uint64_t)fsize + 2000000ULL) { sendText(chat, "The SD card is full."); return; }
  if (libraryCount() >= MAX_SONGS) { sendText(chat, "The library is full (" + String(MAX_SONGS) + " songs)."); return; }

  char base[64];
  if (fname && fname[0]) strncpy(base, fname, sizeof(base) - 1);
  else if (performer && performer[0] && title && title[0]) snprintf(base, sizeof base, "%s - %s", performer, title);
  else snprintf(base, sizeof base, "song_%ld", msgId);
  base[sizeof(base) - 1] = 0;
  char safe[64]; safeName(base, safe, sizeof safe);
  if (strlen(safe) > 44) { strcpy(safe + 40, ".mp3"); }

  char path[80], tmp[84];
  snprintf(path, sizeof path, "%s/%s", DIR_MUSIC, safe);
  for (int k = 2; k < 50; k++) {
    bool ex; { SdGuard g; ex = SD.exists(path); }
    if (!ex) break;
    char stem[48]; strncpy(stem, safe, sizeof stem - 1); stem[sizeof stem - 1] = 0;
    char* dot = strrchr(stem, '.'); if (dot) *dot = 0;
    snprintf(path, sizeof path, "%s/%.40s-%d.mp3", DIR_MUSIC, stem, k);
  }
  snprintf(tmp, sizeof tmp, "%s.part", path);

  setStatus(TG_DOWNLOADING, safe, 0);
  String b;
  if (!httpGetString(apiUrl("getFile") + "?file_id=" + urlEnc(fileId), b)) { sendText(chat, "Could not get the file from Telegram."); setStatus(TG_WAITING, "Download failed", 0); return; }
  JsonDocument doc;
  if (deserializeJson(doc, b) || !doc["result"]["file_path"].is<const char*>()) { sendText(chat, "Telegram did not give a download link (file too big?)."); setStatus(TG_WAITING, "No download link", 0); return; }
  String fp = doc["result"]["file_path"].as<String>();
  String durl = String("http://") + relayHost + "/file/" + fp;
  if (!downloadFile(durl, fsize, tmp)) { sendText(chat, "Download failed, please send it again."); setStatus(TG_WAITING, "Download failed", 0); return; }
  { SdGuard g; SD.rename(tmp, path); }

  {   // check what the file really is
    uint8_t h[12] = {0}; File t; { SdGuard g; t = SD.open(path, FILE_READ); if (t) { t.read(h, 12); t.close(); } }
    bool ok = !memcmp(h, "ID3", 3) || (h[0] == 0xFF && (h[1] & 0xE0) == 0xE0);
    if (!ok) {
      const char* kind = !memcmp(h + 4, "ftyp", 4) ? "M4A/AAC" : !memcmp(h, "OggS", 4) ? "OGG" : !memcmp(h, "fLaC", 4) ? "FLAC" : !memcmp(h, "RIFF", 4) ? "WAV" : "an unknown format";
      { SdGuard g; SD.remove(path); }
      sendText(chat, String("That file is ") + kind + ", not MP3. The iPod only plays MP3. Please convert it to MP3 first (or find an MP3 version).");
      setStatus(TG_WAITING, "Not an MP3 file", 0);
      return;
    }
  }
  setStatus(TG_PROCESSING, "Reading tags + cover", 100);
  Song s;
  if (!libraryProcessFile(path, s) || s.durSec == 0) {
    SdGuard g; SD.remove(path);
    sendText(chat, "That file is not a playable MP3.");
    setStatus(TG_WAITING, "Not a valid MP3", 0);
    return;
  }
  xQueueSend(songQ, &s, pdMS_TO_TICKS(5000));
  char t[16]; fmtTime(s.durSec, t, sizeof t);
  char last[56]; snprintf(last, sizeof last, "%s - %s", s.artist, s.title);
  portENTER_CRITICAL(&mux); strncpy(st.last, last, sizeof(st.last) - 1); st.added++; portEXIT_CRITICAL(&mux);
  setStatus(TG_WAITING, "Waiting for songs", 0);
  sendText(chat, String("Added: ") + s.artist + " - " + s.title + " (" + t + ")" + (s.hasCover ? "" : "\nNo cover art found in the file."));
}

static void handleUpdate(JsonObject msg) {
  int64_t chat = msg["chat"]["id"].as<int64_t>();
  if (ALLOWED_CHAT_ID == 0) {                    // setup helper: tell the owner their chat id
    sendText(chat, String("Your chat ID is ") + String((long long)chat) + "\nPut it in secrets.h as ALLOWED_CHAT_ID and re-flash.");
    return;
  }
  if (chat != ALLOWED_CHAT_ID) return;           // strangers are ignored
  const char* text = msg["text"] | "";
  long msgId = msg["message_id"] | 0;

  JsonObject a = msg["audio"];
  JsonObject d = msg["document"];
  if (!a.isNull() || !d.isNull()) {
    JsonObject o = !a.isNull() ? a : d;
    const char* mime = o["mime_type"] | "";
    const char* name = o["file_name"] | "";
    size_t nl = strlen(name);
    // Telegram "audio" = music, accept it and check the real content after download.
    // A plain "document" must look like an MP3.
    bool isMp3 = !a.isNull() || (!strcasecmp(mime, "audio/mpeg") || !strcasecmp(mime, "audio/mp3")) || (nl > 4 && !strcasecmp(name + nl - 4, ".mp3"));
    if (!isMp3) { sendText(chat, String("I can only use MP3 files. This one is: ") + (mime[0] ? mime : "unknown type") + " " + name); return; }
    handleFile(chat, o["file_id"] | "", name, o["file_size"] | 0, o["title"] | "", o["performer"] | "", msgId);
  } else if (!strncmp(text, "/list", 5)) {
    int n = libraryCount();
    String r = String(n) + " songs:\n";
    for (int i = 0; i < n && i < 25; i++) { const Song& s = librarySong(i); r += String(i + 1) + ". " + s.artist + " - " + s.title + "\n"; }
    sendText(chat, r);
  } else if (!strncmp(text, "/free", 5)) {
    sendText(chat, String("SD card free: ") + String((unsigned long)(sdFreeBytes() / 1048576ULL)) + " MB of " + String((unsigned long)(sdTotalBytes() / 1048576ULL)) + " MB");
  } else {
    sendText(chat, "Send me MP3 files (as audio or as a file, max 20 MB each) and they will be added to your iPod.\n/list - songs on the iPod\n/free - free space on the SD card");
  }
}

static void pollOnce() {
  String u = apiUrl("getUpdates") + "?timeout=" + String(TG_POLL_TIMEOUT_S) + "&allowed_updates=%5B%22message%22%5D&limit=1";
  if (offsetId) u += "&offset=" + String((long long)offsetId);
  String b;
  if (!httpGetString(u, b, 9000)) { Serial.printf("[tg] poll FAILED, heap=%u\n", (unsigned)ESP.getFreeHeap()); vTaskDelay(pdMS_TO_TICKS(1500)); return; }
  JsonDocument filter;
  filter["result"][0]["update_id"] = true;
  JsonObject fm = filter["result"][0]["message"].to<JsonObject>();
  fm["message_id"] = true; fm["text"] = true; fm["chat"]["id"] = true;
  for (const char* k : { "audio", "document" }) {
    fm[k]["file_id"] = true; fm[k]["file_name"] = true; fm[k]["file_size"] = true;
    fm[k]["mime_type"] = true; fm[k]["title"] = true; fm[k]["performer"] = true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, b, DeserializationOption::Filter(filter))) return;
  for (JsonObject up : doc["result"].as<JsonArray>()) {
    int64_t id = up["update_id"].as<int64_t>();
    offsetId = id + 1;                       // acknowledge before working
    JsonObject m = up["message"];
    if (!m.isNull() && !stopReq) handleUpdate(m);
    if (stopReq) break;
  }
}

static void worker(void*) {
  alive = true;
  Serial.printf("[tg] boot heap=%u largest=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  setStatus(TG_CONNECTING, "Connecting to WiFi...", 0);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  wl_status_t rb = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[tg] begin=%d mode=%d status after begin=%d\n", rb, (int)WiFi.getMode(), (int)WiFi.status());
  Serial.printf("[tg] connecting to SSID \"%s\"...\n", WIFI_SSID);
  uint32_t t0 = millis();
  while (!stopReq && WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) vTaskDelay(pdMS_TO_TICKS(200));

  Serial.printf("[tg] WiFi.status()=%d after %u s (3=connected) heap=%u\n",
                (int)WiFi.status(), (unsigned)((millis() - t0) / 1000), (unsigned)ESP.getFreeHeap());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[tg] WiFi CONNECTED, IP %s\n", WiFi.localIP().toString().c_str());
    // TODO: remove after WiFi debugging — one-time network probe
    IPAddress pIp;
    int pDns = WiFi.hostByName("api.telegram.org", pIp);
    WiFiClient pTcp; bool pRaw = (pDns == 1) && pTcp.connect(pIp, 443);
    Serial.printf("[tg] PROBE dns=%d ip=%s dns1=%s rawTCP443=%s largestBlock=%u\n",
                  pDns, pIp.toString().c_str(), WiFi.dnsIP(0).toString().c_str(),
                  pRaw ? "OK" : "FAIL",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    pTcp.stop();
    setStatus(TG_CONNECTING, "Finding the PC...", 0);   // UDP broadcast discovery
    if (relayResolve()) Serial.printf("[tg] using relay %s\n", relayHost.c_str());
    else                Serial.printf("[tg] DISC found nothing; fallback %s\n", relayHost.c_str());
  }
  if (!stopReq && WiFi.status() != WL_CONNECTED) setStatus(TG_ERROR, "WiFi failed - check secrets.h", 0);
  else if (!stopReq && strlen(BOT_TOKEN) < 20)   setStatus(TG_ERROR, "No bot token in secrets.h", 0);
  else if (!stopReq) {
    setStatus(TG_WAITING, "Waiting for songs", 0);
    // skip messages that arrived while the iPod was off? no: keep them, so nothing is lost
    uint32_t fails = 0;
    while (!stopReq) {
      while (gShotHold) vTaskDelay(pdMS_TO_TICKS(50));   // USB screen dump: stay quiet
      if (WiFi.status() != WL_CONNECTED) {
        setStatus(TG_CONNECTING, "Reconnecting...", 0);
        WiFi.reconnect(); vTaskDelay(pdMS_TO_TICKS(2000));
        if (++fails > 20) { setStatus(TG_ERROR, "WiFi lost", 0); break; }
        continue;
      }
      fails = 0;
      pollOnce();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  // keep an error on screen until the user leaves the WiFi page
  while (!stopReq && st.phase == TG_ERROR) vTaskDelay(pdMS_TO_TICKS(100));
  setStatus(TG_STOPPING, "Turning WiFi off...", 0);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  setStatus(TG_OFF, "", 0);
  alive = false;
  vTaskDelete(nullptr);
}

void telegramStart() {
  if (alive) return;
  if (!songQ) songQ = xQueueCreate(4, sizeof(Song));
  stopReq = false;
  portENTER_CRITICAL(&mux); st.pct = 0; st.line[0] = 0; st.phase = TG_CONNECTING; portEXIT_CRITICAL(&mux);
  alive = true;
  xTaskCreatePinnedToCore(worker, "tg", 20480, nullptr, 1, nullptr, 1);
}

void telegramStop() { if (alive) { stopReq = true; setStatus(TG_STOPPING, "Turning WiFi off...", 0); } }
