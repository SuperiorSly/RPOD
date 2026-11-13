# PROJECT BRIEF: DIY iPod-style MP3 player on an ESP32-2432S028 ("CYD")

You are helping a beginner build a portable music player. The hardware is already wired. Your job is to write ALL the software (C++, Arduino framework) and guide me step by step. I'm on Windows and I'm using you from CMD. Explain things simply, one step at a time, and tell me exactly what to run and what to check. Don't dump everything at once.

## 0. How to work with me
- Work in phases (section 8). Finish a phase, tell me how to test it, and WAIT for my result before the next phase.
- Prefer `arduino-cli` (or PlatformIO) so you can compile and upload from CMD. Ask me for the COM port when needed. I have Arduino IDE installed too.
- Use ESP32 Arduino core **2.0.17** (NOT 3.x). The 3.x download keeps failing for me, and most CYD libraries and the built-in-DAC I2S audio path are tested on 2.0.x.
- If something about the hardware is uncertain, say so and ask me to measure/test. Don't guess pins silently.
- Keep the code in a clean project folder with several files (not one giant .ino).

## 1. What the device is
A small iPod-like music player with a 2.8" colour screen, 7 physical buttons, a battery, and a 3.5mm earphone jack. Songs are stored on a microSD card. I add songs by sending audio files to a **Telegram bot**, and the ESP32 downloads them over WiFi onto the SD card.

## 2. Hardware (exactly what I have)
- **ESP32-2432S028** (CYD): ESP32 module, 2.8" 320x240 ILI9341 TFT, XPT2046 touch, microSD slot, on-board speaker amp (U5, small 8-pin chip, looks like LTK8002D) with a 2-pin "SPEAK" socket, RGB LED, USB-C + Micro-USB, CH340 USB-serial chip.
- **microSD 32GB** in the CYD slot. I have NO card reader, so I can't confirm it's FAT32. If the SD init fails, tell me how to deal with it (e.g. try formatting from the device/other method, or tell me to buy a reader).
- **Battery:** 3.7V 1500mAh LiPo (602270).
- **Charge/boost module X-JL0190A:** USB-C input charges the battery; has a USB-A output (5V boost) and a K pad/button. Battery is soldered to its B+/B-. A USB-A to USB-C cable runs from the module's USB-A to the CYD's USB-C port. Tested: it powers the CYD fine.
- **Buttons:** 7 small tactile buttons (4-leg, wired across diagonal legs).
- **Resistors:** 1k ohm 1/4W (10 total). **Capacitors:** 10uF (16V and 25V). **PAM8043 amp** (NOT used for now).
- Breadboard, jumper wires (M-M and F-M), soldering gear, perfboard, heatsink stickers and a small fan (not needed, skipped), 3.5mm jack breakout board (pads: SLEEVE, RING2, RING1, TIP).
- Multimeter (ANENG A830L).
- The CYD's connectors are 1.25mm sockets, so I used the cables from the kit with female ends.

## 3. Wiring (already done)

### 3.1 Power
Battery -> X-JL0190A (B+/B-) -> USB-A out -> USB-A-to-USB-C cable -> CYD USB-C port. For programming, I plug the CYD into the PC by USB with the battery module UNPLUGGED (never both at once).

### 3.2 Pins used
- **CN1 socket** (pins: GND, IO22, IO27, 3V3): GND, IO22, IO27, 3V3 go to the breadboard.
- **P3 socket** (pins: GND, IO35, IO22, IO21): only **IO35** is used. IO22 there is the same net as CN1's IO22. **IO21 is the screen backlight, leave it untouched by wiring** (software controls it).
- **SPEAK 2-pin socket:** used for audio (see 3.4).
- Please verify against the standard CYD pinout (TFT on HSPI: SCK14 MOSI13 MISO12 CS15 DC2, backlight 21; touch XPT2046 CLK25 MOSI32 MISO39 CS33 IRQ36; SD CS5 SCK18 MISO19 MOSI23; audio amp input DAC on GPIO26; RGB LED 4/16/17; LDR 34; BOOT button GPIO0). Tell me if anything looks off for my board revision.

### 3.3 The 7 buttons
**2 direct buttons** (internal pull-up, active LOW, other leg to GND):
- Button 1 **Play/Pause/Select** -> **IO22**
- Button 3 **Home** -> **IO27**

**5 buttons on a resistor ladder, ONE analog pin: IO35** (ADC1, input-only, no internal pull-up, so the ladder has its own pull-up):
```
 3V3 -> 1k -> 1k -> IO35 line      (2k pull-up)
 GND -> [G] -1k- [A] -1k- [B] -1k- [C] -1k- [D]     (chain)
 Each button: one leg on the IO35 line, other leg on its chain point.
```
| Button | Function | Chain point | Expected IO35 voltage |
|---|---|---|---|
| Idle | none | - | ~3.3V |
| Button 2 | Back | G (direct to GND) | 0V |
| Button 4 | Volume + | A (1k) | ~1.1V |
| Button 5 | Volume - | B (2k) | ~1.65V |
| Button 6 | Up / seek forward / next | C (3k) | ~1.98V |
| Button 7 | Down / seek back / previous | D (4k) | ~2.2V |

I measured these with a multimeter and they were right. ESP32 ADC is nonlinear, so use **wide ranges with a calibration step** (first print raw ADC values for each button over Serial and set thresholds from the real numbers), plus debounce. Only one ladder button is ever pressed at a time.

### 3.4 Audio jack (CURRENT method: SPEAK socket, no soldering on the CYD)
The SPEAK socket is a bridge-tied speaker output: **NEITHER pin is ground**. So nothing on the jack goes to GND and there is no capacitor:
```
 SPEAK wire A -> 1k -> jack TIP and RING1 (joined together)
 SPEAK wire B -> 1k -> jack SLEEVE and RING2 (joined together)
```
(I may use 2x1k in series per side to cut hiss; ask me which one I have currently.) Mono sound on both ears. **Earphones only**, never plug this jack into a phone/PC/amp because the sleeve is not ground. Audio comes from the ESP32 DAC on GPIO26 into the on-board amp.

**Fallback if hiss is too bad (later, not now):** solder one thin wire to the end of resistor **R4** that connects to the ESP32's IO26 pad (the signal before the amp), then: wire -> 10uF cap (+ toward the wire) -> two 1k resistors -> TIP and RING1, with SLEEVE and RING2 to GND. That bypasses the amp.

## 4. Features required
1. **Now Playing screen (first screen):** album cover, band/artist name, song name, song length, progress bar and elapsed time.
2. **Playlist screen (second screen):** list of all songs on the SD card, with the same info style (cover thumbnail if feasible, artist, title, length). Highlight the current selection and mark the song that's playing.
3. **Home sidebar** that slides in from the RIGHT when Home is pressed, containing:
   1. **WiFi** (starts the Telegram song-install mode)
   2. **Shuffle** on/off
   3. **Brightness** control (slider/steps, adjusted with Up/Down, confirmed/closed with Back or Select)
   4. **Settings** (colour themes: several selectable theme colours, saved)
   5. (maybe later) extra playlists
4. **Auto screen-off after 20 seconds of no button/touch activity.** The backlight turns off (IO21 PWM to 0) but **music keeps playing**. The first button press only wakes the screen (it should not trigger an action). Make the timeout a constant that is easy to change.
5. Brightness uses PWM on the backlight pin (LEDC), 0-255 in sensible steps (not fully black at the minimum), saved to non-volatile storage and restored at boot.
6. Save settings (theme, brightness, shuffle, volume, last song) in NVS (Preferences) so they survive power-off.
7. **BOOT button (GPIO0):** use as an extra button after boot, e.g. long-press = deep sleep, press = wake. (It cannot cut power fully; full off is the battery module's own button.) Don't need to hold it while plugging in power.
8. Optional: touchscreen support, battery indicator if reasonably possible.

## 5. Button behaviour (7 buttons)
1. **Play/Pause/Select**
   - Now Playing: ONLY play/pause.
   - Playlist: selects/plays the highlighted song; if the highlighted song is the one currently playing, it toggles play/pause instead.
   - Sidebar: select the highlighted item.
2. **Back:** close the sidebar if open; otherwise go to the previous screen (Playlist -> Now Playing). Inside a sub-menu (themes/brightness), go back one level.
3. **Home:** open/close the sidebar (slides from the right).
4. **Volume +** and 5. **Volume -:** work everywhere (short press = one step, hold = repeat).
6. **Up** and 7. **Down:**
   - Playlist and sidebar/menus: move the selection up/down. **No** hold-seek and **no** triple-press here, navigation only.
   - Now Playing: **hold Up = keep skipping forward in 3-second steps; hold Down = keep going back 3-second steps. Triple press Up = next song; triple press Down = previous song.** (There is nothing to "move" in this screen, so they have no navigation role.)
   - Need a clean tap-counter with a timeout (about 400-500 ms) so single press, hold and triple press don't collide.
Tell me if any of these rules conflict and propose a fix.

## 6. Telegram bot song installer
- I'll send audio files (MP3) to my Telegram bot, and the ESP32 downloads them to the SD card (e.g. `/music`).
- Only active when I choose **WiFi** in the sidebar (turn WiFi off again when leaving to save battery and avoid audio noise). Show status on screen (connecting, waiting, downloading with progress, done, error).
- Security: only accept messages from my own **chat ID** (whitelist). Keep the bot token and WiFi credentials in a separate `secrets.h` that I fill in (never hard-coded in the main code).
- Respect the Telegram Bot API limit: bots can download files up to **20 MB**. Handle larger files with a clear on-screen message.
- Sanitize file names for FAT32, avoid overwriting existing files, handle duplicates, and handle the SD card being full.
- Use a lightweight approach (e.g. UniversalTelegramBot + WiFiClientSecure, or direct HTTPS calls to the Bot API with getUpdates and getFile).
- Bot commands (nice to have): `/list`, `/delete <name>`, `/space`.
- Explain how I create the bot with @BotFather and find my chat ID.

## 7. Metadata and UI
- Read ID3 tags (title, artist, album, duration) from MP3 files, and extract the embedded cover art if present, scale it to fit, and cache small cover/metadata data so the playlist loads quickly. Fall back to the file name and a default cover when tags are missing.
- **I will attach an example screenshot of the UI I want. Match its layout, style and feel** as closely as the 320x240 screen allows. Screens are landscape or portrait: choose what fits the example and the hardware orientation and tell me.
- Use a fast display library (TFT_eSPI or LovyanGFX, your pick; give me the exact config for the CYD) and use sprites/partial redraws to avoid flicker. Use TJpg_Decoder (or similar) for covers.
- Smooth sidebar slide animation, but keep the UI responsive while audio plays.

## 8. Phases (stop after each one and wait for me)
**Phase 1: Audio jack test ONLY (do this first).** A tiny standalone sketch to test the earphone jack on its own, so I can judge how bad the hiss is before building anything else. It should:
- Output via the ESP32 DAC on GPIO26 to the on-board amp (core 2.0.17, built-in-DAC I2S mode or the ESP8266Audio library, your choice).
- Serial-menu or button-free test modes, one at a time: (a) silence / DAC at idle (hiss baseline), (b) quiet 440 Hz tone, (c) tone at 3 or 4 volume levels, (d) a fade in/out, (e) optionally generated white noise at very low level to check channel balance.
- Start at a VERY LOW volume (the amp is built for a speaker and can be loud in earphones). Print what's playing over Serial.
- Tell me exactly what to listen for (hiss with nothing playing, whine, pops at start/stop) and which software tricks to try (e.g. ramping the DAC to its midpoint to avoid pops, higher source volume with lower resistor-side gain, disabling WiFi/backlight PWM during the test to see if they add noise).
- Then we decide: keep the SPEAK method, add the second 1k, or go to the R4 solder fallback.
**Phase 2: Buttons.** Sketch that prints raw readings: IO22, IO27 digital and IO35 ADC. I press each button and we set the ladder thresholds together. Then implement debounce, hold detection and triple-press.
**Phase 3: Screen + backlight + touch test** (confirm the pins, orientation, brightness PWM on IO21, 20 s idle timeout).
**Phase 4: SD card** (mount, list files, handle the "not FAT32 / not detected" case, read ID3).
**Phase 5: MP3 playback from SD** with volume, play/pause, seek +-3 s, next/previous, shuffle.
**Phase 6: Full UI** (Now Playing, Playlist, sidebar, themes, brightness, auto screen-off, saved settings).
**Phase 7: Telegram bot song installer.**
**Phase 8: Polish** (battery indicator, sleep via BOOT button, power use, bug fixes, optional extra playlists).

## 9. Safety rules to remember
- Never power from the PC USB and the battery module at the same time.
- IO35 is input-only with no internal pull-up (the ladder has its own). Keep it on ADC1 (works with WiFi on).
- Don't drive or wire IO21 for anything except the backlight.
- Nothing on the earphone jack may touch ground in the SPEAK method. Earphones only, no mic earphones.
- Keep audio volume low in the first tests.

## 10. Start now
Begin with a short summary of your plan and the list of anything you need from me (COM port, which resistor setup I have on the jack, screenshot of my UI, etc.). Then set up the toolchain (arduino-cli with esp32:esp32@2.0.17) and go straight to **Phase 1**.
