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
> Read AGENTS.md and docs/build-with-ai.md in this project. I want the Controls page to have [6] buttons instead of 4,
> for [these switches]. Keep my settings in src/panel_settings.h. Build it with PlatformIO and fix any errors.

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
| `src/ha.h`, `src/ha.cpp` | Home Assistant client. A background FreeRTOS task on core 0 polls each entity with `GET /api/states/<id>` (fast group every 5 s, slow group every 30 s). It does a filtered ArduinoJson parse (state plus a few attributes) and runs queued service calls (`switch/toggle`, `hive/boost_heating_*`). `EntId` lists every entity in order; `IDS[]` maps them to settings. |
| `src/main.cpp` | UI and logic: three pages (`drawHome`, `drawEnergy`, `drawControls`), the status bar, tabs, touch handling, night dimming, the rate LED, the peak chime and the "appliance stopped" alert. Redraws at most once a second, from a snapshot of HA values. |
| `src/board.*` | Display (ST77922 over QSPI), backlight and touch. The UI draws on a 480x320 landscape `GFXcanvas16` in PSRAM; `boardPresent()` rotates it onto the 320x480 portrait panel. |
| `src/touch.*` | Turns raw touches into tap, long press (0.7 s) and swipe left/right. |
| `src/aatext.*`, `src/fonts_aa.*` | Anti-aliased text. `fonts_aa.*` are **generated** by `tools/gen_fonts.py`. |
| `src/alert_audio.*` | Speaker (ES8311 codec over I2S): beeps and chimes. |
| `tools/screenshot.py`, `tools/raw2png.py` | Save the screen over USB as a PNG. |

## Rules for the assistant

- **Build with PlatformIO:** `pio run` to build, `pio run -t upload` to flash. The environment is `cyd35`
  (ESP32-S3, 16 MB flash, 8 MB PSRAM).
- **Settings:** anything user-specific goes in `panel_settings.example.h` as a new `#define` with a plain-English
  comment. Never hard-code the user's entity IDs, IP, token or Wi-Fi in source files. Tell the user to copy any new
  lines into their own `panel_settings.h`.
- **Adding an entity:** add it to `EntId` in `ha.h`, in the slow group (before `E_DEMAND`) or the fast group, and add
  its ID to `IDS[]` in `ha.cpp` in **the same position**. Only `state` and the attributes listed in the filter in
  `fetch()` are kept; add attributes there if you need them.
- **Switching things:** the panel may only toggle the Controls-page switches (`haToggle` refuses anything else). Keep
  that guard. Never add fridges, freezers or anything that must stay on.
- **Text:** fonts only contain printable ASCII plus `° – · × £ → …`. For another character (for example `€`), add
  it to `FULL` in `tools/gen_fonts.py` and run `python3 tools/gen_fonts.py` (needs Pillow). Sizes: `aaR14/17/20/24`,
  `aaB17/20/24/30/44`, `aaB100` (digits and colon only). `aaDraw(cv, x, y, text, font, colour)` uses `y` as the
  **top** of the text and returns its width.
- **Screen layout:** 480x320 landscape. Status bar y 0–28, content y 30–270, tab bar y 274–320. Colours are RGB565 via
  `rgb(r,g,b)`; reuse the palette at the top of `main.cpp`.
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
PlatformIO → Devices in VS Code, shows the port. Sending `1`, `2` or `3` on the serial port switches page first.
