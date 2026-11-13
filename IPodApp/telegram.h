#pragma once
// ============================================================
//  TELEGRAM — WiFi + bot song installer
//  Raw HTTPS + ArduinoJson parsing (no bot library needed):
//  full control of getUpdates, lower heap usage.
//  Active ONLY while the user keeps WiFi open in the sidebar.
//
//  Bot commands: /list  /space  /delete <name>
//  Songs: audio or document messages ≤ 20 MB -> /music/<name>(2)...
// ============================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static bool     g_wifiOn        = false;
static uint32_t g_botNextPoll   = 0;
static long     g_lastUpdateId  = 0;
static bool     g_botDownloading = false;

// status shown by the WiFi menu pane (ui.h owns the drawing)
enum WifiStage { W_OFF, W_CONNECT, W_WAIT, W_PROGRESS, W_DONE, W_ERR };
static WifiStage g_wifiStage = W_OFF;
static char g_wifiMsg[64] = "";
static int  g_wifiPct     = 0;
static volatile bool g_uiWifiDirty = true;   // set true -> ui redraws status pane

static void wifiSetStatus(WifiStage s, const char* msg, int pct)
{
  g_wifiStage = s;
  strncpy(g_wifiMsg, msg, sizeof(g_wifiMsg) - 1);
  g_wifiMsg[sizeof(g_wifiMsg) - 1] = 0;
  g_wifiPct = pct;
  g_uiWifiDirty = true;
}

static void wifiStart()
{
  if (!WIFI_CONFIGURED) { wifiSetStatus(W_ERR, "Edit secrets.h first!", 0); return; }
  if (g_wifiOn) return;
  wifiSetStatus(W_CONNECT, "Connecting WiFi...", 0);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  g_wifiOn = true;
  g_botNextPoll = millis();
}

static void wifiStop()
{
  if (!g_wifiOn) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  g_wifiOn = false;
  wifiSetStatus(W_OFF, "", 0);
}

// ---------------- small bot-API helpers ----------------
static bool tgGet(const char* url, String& resp, uint32_t timeoutMs = 6000)
{
  WiFiClientSecure c; c.setInsecure();
  HTTPClient http;
  if (!http.begin(c, url)) return false;
  http.setTimeout(timeoutMs);
  int code = http.GET();
  if (code == 200) resp = http.getString();
  http.end();
  return code == 200;
}

static void botReply(const char* chatId, const char* text)
{
  JsonDocument doc;
  doc["chat_id"] = chatId;
  doc["text"]    = text;
  char url[80];
  snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", BOT_TOKEN);

  String body, resp;
  serializeJson(doc, body);
  WiFiClientSecure c; c.setInsecure();
  HTTPClient http;
  if (!http.begin(c, url)) return;
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.POST(body);
  http.end();
}

// ---------------- download one file to SD ----------------
static bool telegramDownload(const char* fileId, const char* fileName)
{
  char url[200];

  // 1) getFile -> file_path
  snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getFile?file_id=%s", BOT_TOKEN, fileId);
  String resp;
  if (!tgGet(url, resp)) { wifiSetStatus(W_ERR, "getFile: no answer", 0); return false; }

  JsonDocument d;
  if (deserializeJson(d, resp) != DeserializationError::Ok || !(bool)d["ok"]) {
    wifiSetStatus(W_ERR, "getFile: bad reply", 0);
    return false;
  }
  const char* fpath = d["result"]["file_path"] | "";
  if (!*fpath) { wifiSetStatus(W_ERR, "no file_path", 0); return false; }

  // 2) stream the bytes
  snprintf(url, sizeof(url), "https://api.telegram.org/file/bot%s/%s", BOT_TOKEN, fpath);
  WiFiClientSecure c; c.setInsecure();
  HTTPClient http;
  if (!http.begin(c, url)) { wifiSetStatus(W_ERR, "HTTP begin failed", 0); return false; }
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) { http.end(); wifiSetStatus(W_ERR, "download failed", 0); return false; }
  int total = http.getSize();                            // -1 = chunked
  if (total > 20 * 1024 * 1024) {                       // bot API cap (spec)
    http.end(); wifiSetStatus(W_ERR, "over 20 MB limit", 0);
    return false;
  }
  WiFiClient* s = http.getStreamPtr();
  if (!s) { http.end(); wifiSetStatus(W_ERR, "no stream", 0); return false; }

  // safe FAT32 file name (letters/digits/_-. ; truncate to 40 chars)
  char sane[48];
  int q = 0;
  for (const char* r = fileName; *r && q < (int)sizeof(sane) - 5; r++) {
    if (isalnum((int)*r) || *r == '_' || *r == '-' || *r == '.') sane[q++] = *r;
    else sane[q++] = '_';
  }
  sane[q] = 0;
  size_t sl = strlen(sane);
  if (sl < 4 || strcasecmp(sane + sl - 4, ".mp3") != 0) {
    strncat(sane, ".mp3", sizeof(sane) - 1 - sl);
  }

  // avoid overwrite: find a free "(2)" / "(3)" ... variant
  char target[64]; snprintf(target, sizeof(target), "/music/%s", sane);
  for (int v = 2; SD.exists(target) && v < 50; v++) {
    snprintf(target, sizeof(target), "/music/%.*s(%d).mp3", (int)(strlen(sane) - 4), sane, v);
  }

  File f;
  { SDLock l; f = SD.open(target, FILE_WRITE); }
  if (!f) { http.end(); wifiSetStatus(W_ERR, "SD open failed", 0); return false; }

  static uint8_t buf[2048];
  uint32_t done = 0, nextReport = 0;
  int er = 0;
  while (http.connected() && (total < 0 || done < (uint32_t)total)) {
    size_t avail = s->available();
    if (avail == 0) { er++; if (er > 600) break; delay(10); continue; }
    er = 0;
    size_t take = min(avail, sizeof(buf));
    { SDLock l;
      size_t rd = s->read(buf, take);
      f.write(buf, rd);
      done += rd; }
    if (total > 0 && done - nextReport >= 32768) {
      nextReport = done;
      wifiSetStatus(W_PROGRESS, target, (int)((done * 100) / total));
    }
  }
  { SDLock l; f.close(); }
  http.end();

  if (total > 0 && done < (uint32_t)total) {
    { SDLock l; SD.remove(target); }
    wifiSetStatus(W_ERR, "download incomplete", 0);
    return false;
  }
  return true;
}

// ---------------- one poll step ----------------
static void wifiStep()
{
  if (!g_wifiOn) return;

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t t0 = 0;
    if (t0 == 0) t0 = millis();
    if (millis() - t0 > 20000) { wifiStop(); wifiSetStatus(W_ERR, "WiFi connect timeout", 0); }
    return;
  }
  static bool greeted = false;
  if (!greeted) {
    greeted = true;
    wifiSetStatus(W_WAIT, "Send me a song (MP3)!", 0);
  }

  if (g_botDownloading) return;
  if ((int32_t)(millis() - g_botNextPoll) < 0) return;
  g_botNextPoll = millis() + 1200;

  char url[100];
  snprintf(url, sizeof(url),
           "https://api.telegram.org/bot%s/getUpdates?offset=%ld&timeout=0&limit=5",
           BOT_TOKEN, g_lastUpdateId + 1);
  String resp;
  if (!tgGet(url, resp, 8000)) return;

  JsonDocument doc;
  if (deserializeJson(doc, resp) != DeserializationError::Ok) return;
  if (!(bool)doc["ok"]) return;

  long wantChat = atol(CHAT_ID);                       // whitelist chat id
  if (wantChat == 0) return;

  for (JsonObject res : doc["result"].as<JsonArray>()) {
    g_lastUpdateId = res["update_id"] | g_lastUpdateId;

    JsonVariant msg = res["message"];
    if (msg.isNull()) continue;

    long cid = msg["chat"]["id"] | 0L;
    if (cid != wantChat) continue;
    char cidS[20]; snprintf(cidS, sizeof(cidS), "%ld", cid);

    const char* aid = msg["audio"]["file_id"]    | "";
    const char* afn = msg["audio"]["file_name"]  | "";
    const char* did = msg["document"]["file_id"] | "";
    const char* dfn = msg["document"]["file_name"] | "";
    const char* txt = msg["text"]                | "";

    if (*aid || *did) {
      const char* fileId = *aid ? aid : did;
      String fname = *aid ? String(afn) : String(dfn);
      if (!fname.length()) fname = "song.mp3";

      g_botDownloading = true;
      wifiSetStatus(W_PROGRESS, fname.c_str(), 0);
      bool ok = telegramDownload(fileId, fname.c_str());
      g_botDownloading = false;

      if (ok) {
        playerScan();                                    // rebuild playlist (+ cache dirs)
        wifiSetStatus(W_DONE, "Done! Playlist updated", 100);
        String ack = "Added: " + fname;
        botReply(cidS, ack.c_str());
      } else {
        botReply(cidS, "Adding failed (see the player screen).");
      }
    } else if (*txt) {
      String t(txt);
      if (t == "/list") {
        String s = "Songs on the player:\n";
        for (int k = 0; k < g_trackCount && k < 10; k++)
          s += String(k + 1) + ". " + g_tracks[k].title + "\n";
        if (g_trackCount == 0) s += "(none)";
        botReply(cidS, s.c_str());
      } else if (t == "/space") {
        char b[96];
        snprintf(b, sizeof(b), "Free: %.1f MB / total: %.1f MB. Bot limit: 20 MB per file.",
                 (float)(SD.totalBytes() - SD.usedBytes()) / 1e6, (float)SD.totalBytes() / 1e6);
        botReply(cidS, b);
      } else if (t.startsWith("/delete ")) {
        String nm = t.substring(8); nm.trim();
        String path = "/music/" + nm;
        { SDLock l; SD.remove(path); }
        playerScan();
        botReply(cidS, "Deleted (if it existed).");
      } else if (t == "/start" || t == "/help") {
        botReply(cidS,
                 "Hi! Send me an MP3 (audio or file) and I'll put it on the player.\n/List, /space, /delete <name>");
      } else {
        botReply(cidS, "Send me an MP3 file, or use /list /space /delete <name>");
      }
    }
  }
}
