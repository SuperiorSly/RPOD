# iPod player – ESP32-2432S028 (CYD)

Folders
- `ipod_player/`  the real firmware (open `ipod_player.ino`)
- `display_test/` tiny sketch to find the right screen settings (flash it first if the screen looks wrong)

## 1. Arduino IDE setup (once)
1. **Boards Manager** → install **esp32 by Espressif Systems**, version **2.0.17** (not 3.x).
2. **Library Manager** → install these exact versions:
   - **lvgl 8.3.11** (NOT 9.x)
   - **LovyanGFX 1.1.16**
   - **ESP8266Audio 1.9.7**
   - **TJpg_Decoder 1.1.0**
   - **ArduinoJson 7.x**
3. Copy `ipod_player/lv_conf.h` to `Documents/Arduino/libraries/` (next to the `lvgl` folder, NOT inside it).
4. Tools menu: Board **ESP32 Dev Module**, Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)**, Upload Speed 460800 (921600 if it works).

## 2. Screen check (first time only)
Flash `display_test`. It cycles 4 settings every 5 s. Note the one that shows **RED, GREEN, BLUE bars and white text on black**, then edit `ipod_player/config.h`:
- ILI9341 → `PANEL_ST7789 0`, ST7789 → `PANEL_ST7789 1`
- `PANEL_INVERT 0/1` as shown
- if red and blue are swapped: `PANEL_BGR 1`
- upside down: `SCREEN_ROTATION 2`

## 3. SD card
FAT32 (not exFAT). Put MP3s in a folder named `music` if you want to add songs by PC. Songs sent by Telegram are saved there automatically. The player creates a folder `ipod` for covers and its index – don't delete it unless you want covers rebuilt.

## 4. Telegram bot (once)
1. In Telegram open **@BotFather** → `/newbot` → copy the **token**.
2. Open `ipod_player/secrets.h` and fill `WIFI_SSID`, `WIFI_PASSWORD`, `BOT_TOKEN`. (WiFi must be 2.4 GHz.) Never share this file.
3. Flash, open WiFi mode on the iPod (Home → WiFi), then send any message to your bot. While `ALLOWED_CHAT_ID` is 0 the bot replies with **your chat ID**. Put that number in `secrets.h` and flash again. From then on only you can add songs.

Using it: Home → WiFi, wait for "Waiting for songs", then send MP3 files to the bot (as audio or as a file, max **20 MB** each, Telegram's bot limit). The bot answers "Added: Artist – Title (m:ss)". Commands: `/list`, `/free`. Back keeps WiFi running; Play on the WiFi page turns it off. Music is stopped while WiFi is on.

## 5. Buttons
| Button | Now Playing | PlayList | Menu |
|---|---|---|---|
| Play | play / pause | play highlighted song (on the current song = pause/resume) | select |
| Back | go to PlayList | go to Now Playing | back / close |
| Home | open menu | open menu | close |
| Up / Down | hold = skip ±3 s, 3 quick presses = next / previous | move highlight | move highlight (Brightness: ± step) |
| Vol+ / Vol− | volume | volume | volume |

Screen turns off after 20 s with no button (music keeps playing). The first press only wakes it.

## 6. Hardware test
Hold **Play** while powering on. The screen shows the live IO35 ladder voltage, button names, SD status and a **tone generator**: Play cycles 440 Hz tone / 0 V / 1.65 V / 3.3 V on the DAC (measure these on the R4 pad), Vol± change volume. If a ladder button is not recognised, compare its mV with the limits in `config.h`.

## 7. Known limits (honest list)
- Written and compiled, **not yet tested on real hardware**. Expect to fix small things.
- Bluetooth: the on/off switch is saved but there is **no Bluetooth audio** yet.
- Covers: baseline JPEG only (PNG / progressive JPEG show a default icon). Text: built-in font supports English letters; accents are simplified (é→e); Arabic and other scripts show "?" or the file name.
- Telegram HTTPS does not verify the server certificate (fine for a gift, not for secrets).
- DAC audio is 8-bit, so some background hiss is normal; try `AUDIO_MAX_GAIN_PCT` lower or `AUDIO_DITHER 1`.
- Seeking in VBR MP3s is approximate.
