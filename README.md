# RPOD

A DIY iPod-style MP3 player built on an ESP32 **"Cheap Yellow Display"** (ESP32-2432S028), with a full LVGL touch UI and a **Telegram bot** that loads new songs onto the device over WiFi.

```
+--------------------------------------------------+
|  phone: send .mp3 to the Telegram bot            |
|        |                                         |
|        v                                         |
|  Telegram API <--- HTTPS --- PC relay script     |
|                                  ^               |
|                                  | plain LAN    |
|                                  | HTTP         |
|                         +--------+--------+      |
|                         |  RPOD (ESP32)   |      |
|                         |  UI + SD + I2S  |      |
|                         +-----------------+      |
+--------------------------------------------------+
```

---

## Features

- **2.8" portrait UI** (240x320, dark theme with green accent): Now Playing, Playlist, Menu, Settings screens
- **Music library from microSD** (FAT32, `/music/` folder)
- **Telegram download bot**: send an `.mp3` to the bot and it appears on the SD card. Files are validated by magic bytes (ID3 / MPEG sync) — MP4/M4A, OGG, FLAC and WAV are rejected with a message naming the format
- **Boot intro**: a Pacifico-font "Hello Sly" greeting with a beating heart outline and sparkles — pure LVGL animation, no image decoding, any button skips it
- **Self-configuring relay**: at every boot the device broadcasts `IPIPOD1?` on UDP 8461; the relay answers `IPIPOD1!` — so the PC's IP can change (DHCP) and the bot keeps working
- **Streaming downloads**: the relay pipes Telegram's file straight through, so the device sees download progress live instead of buffering a whole file in RAM
- **Physical buttons** for play/pause, next/previous and volume; hold PLAY at power-on for a hardware test screen (DAC tone generator + button calibration)
- **Linear volume curve** on the internal 8-bit DAC (the default 60% sits in the comfortable range)
- **WiFi stays off at boot** — it is only turned on from Menu > WiFi, when you want to add songs
- **Bluetooth audio mode**: with a VHM-314 receiver board, turning Bluetooth on mutes the ESP32's own DAC so the receiver's audio is the only sound; pairing happens on your phone
- 14 UI themes (accent colour) and brightness settings stored in flash; long theme list scrolls

## The UI

Portrait 240x320, dark theme with a warm accent colour: Now Playing (cover art, song title,
progress bar), PlayList, and a slide-over Menu (WiFi, Shuffle, Brightness, Bluetooth, Settings).
The WiFi page turns the radio on only while it is open; the Bluetooth page switches between the
SD-card player and the external receiver. Themes are picked from a scrollable 14-colour list.
The boot intro greets with a Pacifico-font animation.

## Hardware

| Part | Detail |
|---|---|
| MCU + display | ESP32-2432S028 "CYD", 2-USB variant, **ST7789** panel (config.h has `PANEL_ST7789 1`) |
| Storage | 32 GB microSD in the CYD slot |
| Audio | ESP32 internal DAC on **GPIO26** wired to a 3.5 mm jack, or a VHM-314 Bluetooth receiver in its place (Menu > Bluetooth mutes the DAC while the receiver plays) |
| Power | 3.7 V LiPo + USB charge/boost module into the CYD's USB-C |
| Input | 7 tactile buttons |

Flash the main firmware with the **Huge APP (3 MB / no OTA)** partition scheme — the UI + bot + audio stack needs the room.

## Repository layout

```
ipod_player/              the main firmware (Arduino sketch, multi-file C++)
  ipod_player.ino         setup/loop + hardware test entry
  ui.cpp / ui.h           LVGL screens, themes, intro animation
  player.cpp              playback state machine
  audio.cpp               I2S + MP3 decode (libhelix)
  library.cpp             SD music library scan / sort
  telegram.cpp            WiFi + bot polling + download-to-SD
  hwtest.cpp              tone generator & button test
  config.h                all knobs (pins, sizes, INTRO_*, TG_RELAY_HOST fallback)
  lv_conf.h               LVGL 8.3 configuration used by the build
  font_pacifico_32.c      generated Pacifico font (glyphs for "Hello Sly" only)
  heart_img.c             generated 220x200 4-bit alpha heart outline
  secrets.h               WiFi + bot credentials  <-- NOT committed (gitignored)
IPodApp/                  previous prototype firmware (kept for reference)
Phase1_JackTest/          bring-up sketch: audio jack / DAC output test
Phase2_Buttons/           bring-up sketch: button wiring test
SDBridge/                 bring-up sketch: microSD smoke test
ipod_project_prompt.md    the original project brief
ui_spec.md                the UI design specification
tg_relay.example.ps1      template for the PC-side Telegram relay
tg_relay_start.bat        launcher stub (points at the local, uncommitted relay)
```

## Building & flashing

Toolchain used: **ESP32 Arduino core 2.0.17** (2.x tested — 3.x breaks the built-in-DAC I2S path and several CYD libraries), LVGL **8.3.11**, LovyanGFX, ESP8266Audio, ArduinoJson.

```powershell
arduino-cli core install esp32:esp32
arduino-cli lib install "lvgl@8.3.11" "LovyanGFX" "ESP8266Audio" "ArduinoJson"

# compile (Huge APP partition scheme is required)
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" `
  --build-path <build-dir> "<repo>/ipod_project (1)/ipod_project/ipod_player"

# flash
arduino-cli upload -p COM4 --input-dir <build-dir> `
  "<repo>/ipod_project (1)/ipod_project/ipod_player"
```

`lv_conf.h` ships inside the sketch folder so the build picks it up automatically.

### secrets.h (create it yourself — it is gitignored)

```c
#define WIFI_SSID        "your-2.4GHz-wifi"
#define WIFI_PASSWORD    "your-wifi-password"
#define BOT_TOKEN        "1234567890:AAAA-your-telegram-bot-token"   // from @BotFather
#define ALLOWED_CHAT_ID  123456789LL      // your Telegram numeric chat id
```

Only the chat id listed here is allowed to command the bot — any other sender is ignored.

## The PC-side relay (why it exists)

The ESP32 cannot run WiFi **and** TLS at the same time — the HTTPS handshake dies from RAM fragmentation on a 520 KB-heap part. RPOD therefore talks **plain HTTP on the LAN** to a tiny PowerShell relay, which does the HTTPS to Telegram for it:

```text
iPod --(HTTP 192.168.x.x:8461)--> tg_relay.ps1 --(HTTPS)--> api.telegram.org
```

1. Copy `tg_relay.example.ps1` to `tg_relay.ps1` on any always-on PC on the same LAN.
2. Paste your bot token into `$Token` (the token never leaves the PC).
3. Run it (double-click the `.ps1` or the included `.bat`); add it to startup for 24/7 operation.
4. The **device finds the PC by itself** — it broadcasts `IPIPOD1?` on UDP 8461 at every boot and uses the responder's address. The `TG_RELAY_HOST` fallback in `config.h` is only used if discovery finds nothing.
5. Send an `.mp3` to the bot — it replies "Added: <name>" and the song shows up in the Playlist.

Downloads stream through the relay (headers forwarded immediately, body piped), so even multi-MB songs download reliably with the device's tiny heap.

## Fonts / the boot intro

The intro is generated, not hand-drawn: `font_pacifico_32.c` was produced with
[lv_font_conv](https://github.com/Martin-Grave-Ejstrup/lv_font_conv) from Pacifico (SIL OFL) and
contains **only the glyphs used in "Hello Sly"** to stay small; `heart_img.c` is a generated
220x200 4-bit alpha heart outline recoloured by LVGL's image recolor to the theme accent.
If you change the greeting text, regenerate the font with the new `--symbols` list.
Boot behaviour knobs live in `config.h`: `INTRO_ENABLED`, `INTRO_NAME`, `INTRO_MS`.

## Notes & limitations

- The bot is only reachable while the relay script is running on the PC **and** WiFi has been turned on from Menu > WiFi (the radio is off at boot).
- The VHM-314 is a standalone receiver: the ESP32 cannot pair or see its connection. Bluetooth mode simply silences the internal player; pair and play from your phone.
- MP3 only — the bot rejects other audio formats on purpose (the decoder is MP3-only).
- The display is driven through LovyanGFX with the ST7789 branch enabled; the ILI9341 variant
  of the same board flips one config flag.
- Uploads to COM4 occasionally hit a transient "chip stopped responding" — a second attempt always succeeds.

## License

**Proprietary — all rights reserved.** See [LICENSE](LICENSE).
Viewing the code here is fine; using, copying, modifying or redistributing any part of it is **not** permitted without written permission.
