// ============================================================
//  SDBridge — one-shot file transfer tool (PC -> SD over COM)
//  Receives an MP3 over the USB serial link and writes it to
//  /music/ on the CYD's micro SD card. Used to load songs
//  without pulling the card out. The iPod app is flashed back
//  right after the transfer.
//
//  Protocol (PC side: sd_send.ps1)
//    PC: "IPSD\n"          -> device: "READY" or "NOMOUNT"
//    PC: "FILE <name> <size>\n" -> "GO" (or "NOFILE")
//    PC: 2 KB chunks       -> device acks each with "K\n"
//    PC: "END\n"           -> "OK <bytes>", then "L <name> <size>" list, "LISTDONE"
// ============================================================
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

static File     cur;
static uint32_t totalRem = 0, chunkRem = 0, written = 0;
static bool     dataMode = false;

static bool mountSD()
{
  SPI.begin(18, 19, 23, 5);                    // CYD SD = VSPI, CS5
  if (SD.begin(5, SPI, 20000000)) return true; // fast first
  if (SD.begin(5, SPI,  4000000)) return true; // fallback slow
  return false;
}

static void listMusic()
{
  File d = SD.open("/music");
  if (!d) { Serial.println("LIST 0"); return; }
  int n = 0;
  File f = d.openNextFile();
  while (f) {
    if (!f.isDirectory())
      Serial.printf("L %s %u\n", f.name(), (unsigned)f.size());
    f.close();
    f = d.openNextFile();
    n++;
  }
  d.close();
  Serial.printf("LISTDONE %d\n", n);
}

static void handleCmd(const String& line)
{
  if (line == "IPSD") {
    if (mountSD()) { SD.mkdir("/music"); Serial.println("READY"); }
    else             Serial.println("NOMOUNT");
  } else if (line.startsWith("FILE ")) {
    int sp   = line.lastIndexOf(' ');
    String name = line.substring(5, sp);
    uint32_t sz = (uint32_t)line.substring(sp + 1).toInt();
    String path = "/music/" + name;
    SD.remove(path);                           // overwrite previous copy
    cur = SD.open(path, FILE_WRITE);
    if (!cur) { Serial.println("NOFILE"); return; }
    totalRem = sz; written = 0;
    chunkRem = (sz < 2048) ? sz : 2048;
    dataMode = true;
    Serial.println("GO");
  } else if (line == "END") {
    if (cur) { cur.flush(); cur.close(); }
    dataMode = false;
    Serial.printf("OK %u\n", (unsigned)written);
    listMusic();
  } else if (line == "PING") {
    Serial.println("PONG");
  }
}

static uint8_t buf[1024];

static void pumpData()
{
  while (chunkRem) {
    if (totalRem == 0) break;
    int n = (int)chunkRem;
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    int av = Serial.available();
    if (av <= 0) return;                       // chunk still in flight
    if (n > av) n = av;
    int got = Serial.readBytes(buf, n);
    if (got <= 0) return;
    cur.write(buf, got);
    written   += got;
    totalRem  -= got;
    chunkRem  -= got;
  }
  if (totalRem == 0 && chunkRem == 0) {
    cur.flush(); cur.close();
    dataMode = false;
    Serial.printf("K\nFILEOK %u\n", (unsigned)written);
  } else {
    Serial.print("K\n");                       // chunk complete, ack it
    chunkRem = (totalRem < 2048) ? totalRem : 2048;
  }
}

void setup()
{
  Serial.setRxBufferSize(8192);
  Serial.begin(460800);
  delay(200);
  Serial.println("[bridge] up, waiting for transfer");
}

void loop()
{
  if (dataMode) { pumpData(); return; }

  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handleCmd(line); line = ""; return; }
    if (c != '\r') line += c;
    if (line.length() > 90) line = "";
  }
}
