# CYD Home Panel

A small 3.5" touch screen that shows your **Home Assistant** home at a glance: the time, inside and outside
temperature, heating, live electricity use and cost, the current electricity price, and up to 8 switches for lights
and plugs. It works the right way up in any of four orientations, portrait or landscape.

It runs on a cheap ESP32-S3 3.5" "CYD" (Cheap Yellow Display) board. There is no soldering and no wiring: you plug it
into USB, load this program onto it once, and it talks to Home Assistant over your Wi-Fi.

![Home, Energy and Controls in landscape (top) and portrait with the hidden menu (bottom)](docs/screenshot.png)

**New to this? Follow the step-by-step guide: [docs/setup-guide.md](docs/setup-guide.md).** It assumes no experience
and takes about an hour, most of it waiting for downloads.

**Want it different?** Use your own AI assistant to change it: [docs/build-with-ai.md](docs/build-with-ai.md) has
ready-made prompts and everything the assistant needs to know about the code.

## What it shows

**Changing page:** tap near the left or right edge of the screen, or swipe. The dots at the bottom show which page
you're on.

- **Home:** a big clock, the date, inside and outside temperature, the weather, and the heating. In portrait it also
  shows today's cost and use.
  With Hive heating, **hold your finger on the Heating card** to boost the heating for 30 minutes (to the room
  temperature +1 °C, between 20 and 25 °C). It asks first: tap **Start** to confirm. Hold it again to stop the boost.
- **Energy:** how much power the whole house is using right now (split into "plugs" and "everything else"), today's
  cost and kWh, the current electricity price and when the next price starts, and your top 8 smart plugs by power.
- **Controls:** up to 8 on/off switches for lights and plugs. The page opens **locked** so a stray touch can't
  switch anything: tap **Tap to unlock** at the bottom first. It locks again by itself 25 seconds after your last tap.
  Anything you choose (for example a desk with a computer) can also need a second tap to turn off.

**Hidden menu:** hold the page name (top left) for about a second. It has:

- **Rotate:** turns the picture 90° per tap (portrait, landscape and both upside-down versions);
- **Brightness − / +**;
- **System info:** Wi-Fi, Home Assistant connection, memory and uptime.

Rotation and brightness are saved on the board.

Extras:

- **Price colours:** green = cheap, amber = standard, red = peak. The line under the status bar and the small RGB LED
  on the back of the board show the current price. The LED is kept dim and turns off at night. A chime plays when the
  peak price starts.
- **"HA offline" warning:** shown in red if Home Assistant stops answering for a minute.
- **Night mode:** the screen dims at night (23:00–07:00 by default). A touch wakes it for 30 seconds.
- **Optional "appliance stopped" alert:** a red banner and a chime when an appliance that should be running uses
  almost no power, for example a dehumidifier with a full tank.

## What you need

| | |
|---|---|
| Screen board | ESP32-S3 3.5" CYD touch board, ST77922, 320x480, with speaker (Amazon UK **B0HGQ5XMQ8**) |
| Cable | A USB-C cable that carries **data** (not a charge-only one) |
| Power | Any USB phone charger, once it is set up |
| Home Assistant | Already running on your home network |
| Optional | [Octopus Energy integration](https://github.com/BottlecapDave/HomeAssistant-OctopusEnergy) for prices and cost (the "using now" figure needs an Octopus Home Mini), Hive for the boost button, smart plugs that report power |

Anything you don't have is simply left blank in the settings, and that part of the screen stays empty.

## Settings

Your settings (Wi-Fi, Home Assistant address and token, which sensors and switches to show, prices, night hours) live
in one file, `src/panel_settings.h`, made from [`src/panel_settings.example.h`](src/panel_settings.example.h). It is
never uploaded if you share your copy of this project. The [setup guide](docs/setup-guide.md) walks you through it.

## Safety notes

- The panel can only switch the entities you put on the Controls page, and only after **Tap to unlock**. Don't put a
  fridge, freezer or anything else that must stay on there.
- A tap is sent as "turn on" or "turn off" for the state you saw. If Home Assistant is unreachable, the tap is dropped,
  never sent minutes later.
- The alert entity is only watched, never switched.
- The access token lets the panel control Home Assistant. For extra safety you can create a separate, non-admin Home
  Assistant user just for the panel and make the token from that user ([guide, step 4](docs/setup-guide.md#4-get-the-details-from-home-assistant)).
- If you lose the board, delete its token in Home Assistant.

## For developers

Build with `pio run`, flash with `pio run -t upload`, logs with `pio device monitor`. The code map, layout rules and
how to take screenshots over USB are in [docs/build-with-ai.md](docs/build-with-ai.md). The display, touch and audio
drivers come from [DPFSentinel-ESP32-S3](https://github.com/vitorhpsousa/DPFSentinel-ESP32-S3), which documents the
board in detail.

## Ideas and contributions

This is a personal project shared as-is. Ideas, questions and photos of your own panel are welcome in
**[Discussions](../../discussions)**. Found a bug? Open an **[issue](../../issues)**.

Ideas not built yet:

- **Install from the browser:** a one-click web installer, plus a way to enter Wi-Fi and Home Assistant details on
  the board (for example a setup hotspot), so nobody needs VS Code or to edit a file.
- **LED status:** the LED shows only the price today. It could also blink when Home Assistant is offline or an alert
  is active.
- **Smoother "Using now":** a boiler or heat pump that cycles makes the figure jump every minute (for example 0.9 →
  7 kW). It could show an average, or a "heating on" marker.
- Other thermostats (Nest, Tado, generic `climate`) for the boost button.
- Other energy suppliers, or a fixed price, instead of Octopus.
- More pages: solar, battery, EV charging.

## Licence

GPL-3.0 (see [LICENSE](LICENSE)), because the board drivers come from a GPL-3.0 project. The DejaVu fonts have their own
free licence; see [tools/fonts/README](tools/fonts/README).
