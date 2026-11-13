# UI SPECIFICATION (add this to the main project brief)

I attached a screenshot of the UI I designed. It's a photo of a monitor showing a UI design tool (it looks like SquareLine Studio / LVGL-style editor). **Only the three dark screens are the real UI.** Ignore the editor around them: the lock icons on the far left, the property panel on the right, the mouse cursor, and the scrollbar. Also ignore the small tabs above each screen ("Now Playing" in green, "PlayList", "Menu"). Those are the editor's screen-name labels, not part of the UI.

Match this design as closely as the hardware allows.

## Display
- Portrait orientation, about 240x320 (the CYD panel is natively 320x240 landscape, so set the rotation to portrait and make sure touch and drawing agree).
- Dark theme: near-black / very dark navy background, white text, one accent colour (bright green in my mock-up).
- Rounded sans-serif font (Montserrat-like). Bold for song titles, regular/lighter for artist names.
- Rounded corners on cards and bars, thin light-grey borders.
- Strongly recommended: LVGL (smooth animations, rounded widgets, easy theming). Use TFT_eSPI/LovyanGFX only if you can justify it. Keep audio decoding on its own task/core so animations never make the sound stutter.

## Screen 1: NOW PLAYING (the first screen shown at boot)
Top to bottom:
1. Centered white title text "Now Playing" at the top (not a coloured bar; the green bar in the screenshot is the editor's label).
2. **Large album cover**, centered, roughly 75-80% of the screen width and nearly square. Fill it with the song's embedded cover art (scaled). Use a default cover if none.
3. **Song title** below the cover, left-aligned with the cover's left edge, large white bold text (example: "My Immortal"). Long titles should scroll horizontally (marquee) or truncate with "...".
4. **Artist / band name** directly under the title, smaller and lighter (example: "Evanescence").
5. **Progress bar**: thin, fully rounded bar spanning the cover's width. Light track (grey) with a **white filled portion** showing playback progress.
6. Under the bar: **elapsed time on the left** ("2:11") and **total length on the right** ("4:22"), small white text, format m:ss.
Nothing else on this screen (no big buttons). The physical buttons control it. A small volume indicator and a small shuffle/play-state icon may appear briefly as an overlay, and must not change the layout above.

## Screen 2: PLAYLIST
1. Centered title "PlayList" in a slightly lighter dark header strip with a thin darker divider under it.
2. A vertical list of **song rows**. Each row is a rounded rectangle with a thin light-grey/blue-grey outline and a dark fill, about 1/4 of the screen tall, small gap between rows.
   - **Left:** square cover thumbnail (inside the rounded border, small margin).
   - **Middle:** song title on top (white, bold-ish, larger), artist underneath (smaller, lighter).
   - **Right:** duration right-aligned and vertically centered ("4:22", "4:55", "6:54").
3. About 3 rows are visible at a time. The list scrolls when there are more songs.
4. The mock-up doesn't show a selection state, so design one: the highlighted row gets a coloured border/outline in the **accent colour** (and optionally a slightly lighter fill). The song that is currently playing gets a small playing indicator (small icon or accent-coloured title text).
5. Up/Down move the highlight; the list auto-scrolls to keep it visible.

## Screen 3: MENU (the sidebar)
- It's an overlay, not a full screen. The **Playlist screen stays visible but dimmed on the left**, and the **menu panel slides in from the RIGHT edge**, covering about 60% of the width and the full height.
- The panel has a slightly different (a bit darker/flatter) background than the screen.
- At the top of the panel, a large white heading "Menu".
- Under it, a vertical stack of **menu items**, each a rectangle with a thin light-grey outline and dark fill, small text left-aligned: in my mock-up "Shuffle Playlist" and "Settings".
- **Add these items** in the same style, in this order: **WiFi** (to install songs), **Shuffle Playlist** (show on/off state, e.g. a small "On/Off" label or a switch on the right), **Brightness**, **Settings**.
- The highlighted item uses the accent-colour outline. Up/Down move the highlight, Select activates, Back or Home closes the panel (slide out to the right).
- **Brightness:** selecting it turns the item into a horizontal slider (same style as the progress bar, white fill) with a percentage. Up/Down change it in steps, the screen brightness changes live, and Back/Select confirms and returns to the menu. The minimum must not go fully black.
- **Settings:** opens a sub-list of **colour themes** (accent colour presets, for example green, blue, red, purple, orange), each shown as a row with a colour swatch. The chosen theme recolours the accent (selection outlines, progress highlights, etc.) everywhere and is saved.
- **WiFi:** shows a small status panel (connecting, waiting for songs from Telegram, downloading with a progress bar, done or error) in the same visual style.

## Animations and behaviour
- Sidebar slides in and out horizontally (about 200-250 ms, ease-out). Screen changes between Now Playing and Playlist can use a short horizontal slide or a quick fade.
- Progress bar and elapsed time update every second without redrawing the whole screen.
- Auto screen-off after 20 s of no input (backlight to 0, music keeps playing). The first button press only wakes the screen.
- No flicker. Redraw only what changes. Cover art is decoded once per song and cached, and playlist thumbnails are cached on the SD card in a small format, so scrolling stays smooth.

## Data shown on screens (from the MP3's ID3 tags, with fallbacks)
Title (fallback: file name), artist (fallback: "Unknown Artist"), duration, embedded cover (fallback: default cover image). Album name is not shown on screen.

## Things for you to decide and tell me
- Exact font sizes and pixel layout that fit 240x320 (propose, then I'll review it on the real screen).
- Whether LVGL fits in the ESP32's memory next to the audio decoder and the Telegram HTTPS client (this chip has no PSRAM, so keep buffers small and check free heap). If not, say what you'd cut.
