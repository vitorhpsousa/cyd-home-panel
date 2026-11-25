# Change it with your own AI assistant

You don't need to be a programmer to adapt this panel. An AI coding assistant (Claude Code, ChatGPT, GitHub Copilot,
Cursor and so on) can make the changes for you. This page gives the assistant what it needs to know, and gives you
prompts to start from.

**Set up the plain version first** with the [setup guide](setup-guide.md). Then you know the board, the cable and your
settings all work before you start changing things.

---

## Prompts you can copy

Open the project folder in your assistant (or paste the files it asks for), then paste one of these. Change the parts
in [brackets].

**Make it yours**
> Read AGENTS.md and docs/build-with-ai.md in this project. I want the Controls page to stay unlocked for [60] seconds
> instead of 25, and the [Kettle] switch to need a second tap to turn off. Keep my settings in src/panel_settings.h. Build it with PlatformIO and fix any errors.

**A different thermostat**
> Read AGENTS.md. My heating is a [Nest / Tado / generic climate] thermostat, not Hive. Change the hold-to-boost on the
> Heating card to [raise the target temperature by 1 °C for 30 minutes] using Home Assistant's standard climate services.

**A new page**
> Read AGENTS.md. Add a fourth page called [Solar] that shows [these sensors]. Follow the style of the Energy page.

**Not on Octopus**
> Read AGENTS.md. I'm not on Octopus. Replace the Energy page's price and cost with [my sensors / a fixed price of
> 24.5p per kWh].

Tips:

- Ask for **one change at a time**, build it, try it on the screen, then ask for the next.
- If something goes wrong, paste the **error text** or the **Serial Monitor output** back to the assistant.
- To show the assistant what the screen looks like, take a screenshot over USB (see "Screenshots" below) and attach it.
- **Never paste your token or Wi-Fi password into a chat.** They only belong in `src/panel_settings.h`, which stays
  on your computer.

---

## Project map (for the assistant)

| File | What it does |
|---|---|
| `src/panel_settings.example.h` | Template for all user settings: Wi-Fi, HA address and token, entity IDs, names, prices. The user copies it to `src/panel_settings.h` (git-ignored). Every value is a `#define`; `""` means "not used". |
| `src/settings_check.h` | Includes `panel_settings.h`, or stops the build with a plain-English message. |
| `src/config.h`, `src/config.cpp` | Reads every setting for the rest of the code (`cfgEntity(EntId)`, `cfgPlugName`, `cfgTileName`, …) from `panel_settings.h`. Rotation and brightness are saved on the board (NVS). |
| `src/net.*` | Joins the Wi-Fi and rejoins if it drops. |
| `src/ha.h`, `src/ha.cpp` | Home Assistant client. A background FreeRTOS task on core 0 polls each entity with `GET /api/states/<id>` (fast group every 5 s, slow group every 30 s), with a filtered ArduinoJson parse (state plus a few attributes). Service calls (`switch/turn_on`/`turn_off` via `haSet()`, `hive/boost_heating_*`, and the Log page's `script/turn_on`, `input_select/select_option` and `logbook/log` via `haShower()`, `haFlow()`, `haLog()`) each run in their own short task with their own connection, so a slow Bluetooth device (HA answers only when it has, 5–40 s) never blocks other commands or polling; nothing is queued to run later, and an entity with a command in flight is re-read every second. `EntId` lists every entity in order. |
| `src/main.cpp` | Setup and the main loop: touch handling (swipes, forgiving taps, unlock, confirm, long presses), night dimming, the rate LED, the peak chime and the "appliance stopped" alert. Redraws at most once a second from a snapshot of HA values. |
| `src/ui.h` | Shared UI state, palette and layout helpers: `EDGE` (side margins), `contentL/R/W/Bottom()`, the tap areas (`titleRect`, `heatingRect`, `tileRect`, `controlsBarRect`, …) and the accidental-touch guard (`UiGuard`: unlock 25 s, confirm-off 3 s, boost confirm 8 s). |
| `src/ui_pages.cpp` | Draws the status bar, page dots and the four pages (Log only when set up), each with a landscape and a portrait layout, plus overlays (toast, alert banner, boost confirm). |
| `src/ui_menu.cpp` | The hidden menu (hold the page name): Rotate, Brightness, System info, Power test. |
| `src/sleep.*` | Battery saving (`SLEEP_AFTER_S`): backlight off, display off (DISPOFF), Wi-Fi off, light sleep; the touch INT pin (GPIO 47, low on touch) wakes it. Not SLPIN: in SLPIN the touch controller (part of the display driver) stops reporting taps. |
| `src/power_test.*` | Hidden-menu Power test: six 15 s stages (normal, backlight off, display off, display asleep, + Wi-Fi off, + light sleep) for a USB meter, with a tap check in stages 3 and 4. |
| `src/board.*` | Display (ST77922 over QSPI), backlight and touch. Rotation is set at run time (`boardSetRotation(0..3)`: 0/2 portrait 320x480, 1/3 landscape 480x320); there is one PSRAM canvas per shape and `boardPresent()` rotates it onto the panel. `UI_W`/`UI_H` give the current size. |
| `src/touch.*` | Turns raw touches into tap (20–600 ms, moved ≤ 20 px), long press (0.7 s, stayed within 20 px) and swipe left/right. |
| `src/aatext.*`, `src/fonts_aa.*` | Anti-aliased text. `fonts_aa.*` are **generated** by `tools/gen_fonts.py`. |
| `src/alert_audio.*` | Speaker (ES8311 codec over I2S): beeps and two chimes (`PEAK_CHIME`, `APPLIANCE_ALERT`). |
| `tools/screenshot.py`, `tools/raw2png.py` | Save the screen over USB as a PNG. |

## Rules for the assistant

- **Build with PlatformIO:** `pio run` to build, `pio run -t upload` to flash. The environment is `cyd35`
  (ESP32-S3, 16 MB flash, 8 MB PSRAM).
- **Settings:** anything user-specific goes in `panel_settings.example.h` as a new `#define` with a plain-English
  comment. Never hard-code the user's entity IDs, IP, token or Wi-Fi in source files. Tell the user to copy any new
  lines into their own `panel_settings.h`.
- **Adding an entity:** add it to `EntId` in `ha.h`, in the slow group (before `E_DEMAND`) or the fast group, add a
  setting for it in `panel_settings.example.h`, and add it to the `IDS[]` table in `config.cpp` in **the same
  position** (use `#ifndef` with a `""` default so older settings files still build). Only `state` and the attributes listed in the filter in
  `fetch()` are kept; add attributes there if you need them.
- **Switching things:** the panel may only switch the Controls-page switches (`haSet` refuses anything else), only
  while the page is unlocked. Keep both guards, and keep sending `turn_on`/`turn_off` (never `toggle`), in the tile's own domain (`switch`, `light` or `input_boolean`; anything else is refused). Never add
  fridges, freezers or anything that must stay on. The Log page may only call its own three things (`haShower` runs
  the two configured scripts, `haFlow` only options from `FLOW_OPTIONS`, `haLog` only `logbook.log`), also only while
  unlocked (`pageLocked()`).
- **Text:** fonts contain printable ASCII plus `° · £ → … ↻`. For another character (for example `€`), add it to
  `FULL` in `tools/gen_fonts.py` and run `python3 tools/gen_fonts.py` (needs Pillow). Sizes: `aaR14/17/20`,
  `aaB17/20/24/30`; `aaB36`/`aaB44` have only digits, `- . ° £ k W` (big figures); `aaB80`/`aaB100` only digits,
  `-` and `:` (clock). `aaDraw(cv, x, y, text, font, colour)` uses `y` as the
  **top** of the text and returns its width.
- **Screen layout:** every page has a landscape (480x320) and a portrait (320x480) version; never hard-code sizes, use
  `UI_W`/`UI_H` and the helpers in `ui.h`. Status bar at the top (`STATUS_H`), page dots at the bottom (`DOTS_H`).
  Keep cards out of the `EDGE` margins at the left and right (room to start a swipe). Pages change only by swiping; taps on the Controls page go to the nearest row or bar within `TAP_SLOP` px. Colours are RGB565
  via `rgb(r,g,b)`; reuse the palette in `ui.h`. Check all four rotations with screenshots.
- **Memory:** heap is fine (about 250 KB free). Don't request the HA websocket or `/api/states` (all entities); some
  Octopus sensors carry several KB of attributes.
- **Testing without a screen:** there is no simulator. Build, flash, then take a screenshot (below) and look at it.

## Screenshots

With the board plugged in over USB:

```bash
~/.platformio/penv/bin/python tools/screenshot.py /dev/cu.usbmodem1101 shot.png
```
```bash
python3 tools/raw2png.py shot.png.raw shot.png
```

Use your board's port: `/dev/cu.usbmodem…` on macOS, `/dev/ttyACM0` on Linux, or `COM5` or similar on Windows
(where PlatformIO's Python is `%USERPROFILE%\.platformio\penv\Scripts\python.exe`). Running `pio device list`, or
PlatformIO → Devices in VS Code, shows the port. Keys you can send on the serial port first (they only change what is shown, never switch anything): `1`/`2`/`3`/`4`
page, `r` rotate, `m` menu, `i` system info, `u` lock/unlock look, `c` confirm-off look, `b` boost-confirm look, `a` alert-banner look. `p` runs the Power test, `z` turns sleep off until the next restart.

With `SLEEP_AFTER_S` set, the board sleeps after that many idle seconds and its USB port goes quiet: tap the screen (or send `z` while it is awake) before flashing or taking screenshots.
