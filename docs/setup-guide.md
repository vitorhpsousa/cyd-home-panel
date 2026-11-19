# Setup guide (no experience needed)

This guide takes you from "the screen arrived" to "the screen shows my home". You will install one free program on
your computer, copy some details out of Home Assistant into one settings file, and press an Upload button.
There is no soldering and no wiring.

Allow about an hour the first time. Most of it is waiting for downloads.

Each step ends with a **✅ Check**. Don't move on until the check passes. If something goes wrong, the
[troubleshooting table](#9-troubleshooting) at the end follows the same order as the steps.

---

## 1. What you need

- **The screen board:** ESP32-S3 3.5" CYD touch board (ST77922, 320x480, speaker). Amazon UK **B0HGQ5XMQ8**.
- **A USB-C cable that carries data.** Many cheap cables only charge, and then the computer can't see the board.
  The cable that came with a phone usually works.
- **A computer:** Windows 10/11, macOS or Linux.
- **Home Assistant**, already running on your home network, and a login that can create a token.
- **Your Wi-Fi name and password.** The board only works on **2.4 GHz** Wi-Fi. Most home routers offer 2.4 GHz and
  5 GHz under the same name, which is fine.

Optional, for the Energy page:

- The **Octopus Energy** integration (by BottlecapDave, installed from HACS). It gives prices, cost and kWh.
- An **Octopus Home Mini**, for the live "Using now" figure.
- **Smart plugs** that report power in watts (Meross, Tuya/Zigbee, TP-Link and similar).

---

## 2. Install the tools (once)

You will use **VS Code**, a free editor, with the **PlatformIO** add-on, which loads programs onto boards like this one.

### Windows 10/11
1. Install VS Code from https://code.visualstudio.com/ (the default options are fine).
2. Open VS Code. Click the **Extensions** icon on the left (four little squares). Search for **PlatformIO IDE** and
   click **Install**.
3. Wait until it says PlatformIO Core is installed (this takes several minutes), then restart VS Code when asked.
4. No driver is needed. Windows sets up the board by itself.

### macOS
1. Open the **Terminal** app, paste this line and press Enter:
   ```bash
   xcode-select --install
   ```
   Click **Install** in the window that appears. If it says this is already installed, carry on.
2. Install VS Code from https://code.visualstudio.com/ and drag it into Applications.
3. In VS Code, open **Extensions** (four squares on the left), search for **PlatformIO IDE**, click **Install**, and
   restart VS Code when asked.

### Linux (Ubuntu/Debian)
1. In a terminal: `sudo apt install git python3 python3-venv curl`
2. Install VS Code (the `.deb` from https://code.visualstudio.com/), then install **PlatformIO IDE** from Extensions.
3. Let your user use the USB port, then **log out and back in**:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
   ```
   ```bash
   sudo usermod -a -G dialout,plugdev $USER
   ```

✅ **Check:** VS Code has a small **ant-head icon** (PlatformIO) in the bar on the left.

---

## 3. Download this project

1. At the top of this project's GitHub page, click the green **Code** button, then **Download ZIP**.
2. Unzip it somewhere easy to find, for example your Documents folder.
3. In VS Code choose **File → Open Folder…** and pick the unzipped folder: the one that contains `platformio.ini`.
   If VS Code asks whether you trust the authors, click **Yes**.

PlatformIO now downloads what it needs for this board in the background. The first time this takes a few minutes.

✅ **Check:** the list on the left (Explorer) shows `platformio.ini`, a `src` folder and a `docs` folder.

---

## 4. Get the details from Home Assistant

Open Home Assistant in your browser. You need four things. Paste them into a note as you go.

### a) Home Assistant's IP address
**Settings → System → Network.** Under "Network adapter" you will see an IP address such as `192.168.1.50`. Write
down just the numbers, without the `/24` at the end.

(The address in your browser bar, such as `homeassistant.local:8123`, doesn't work on the board. It needs the numbers.)

### b) An access token
This is like a password that lets the screen talk to Home Assistant.

1. Click your **name/profile** at the bottom of the left menu.
2. Open the **Security** tab and scroll down to **Long-lived access tokens**.
3. Click **Create token**, name it `Wall panel`, and click OK.
4. **Copy the long code it shows straight away.** Home Assistant only shows it once.

> **Tip (optional, safer):** create a separate user for the panel first. Go to **Settings → People → Add person**,
> turn on "Allow person to login", and leave "Administrator" **off**. Log in as that user, then make the token there.
> The panel still works, but the token can't change Home Assistant's settings.

### c) Your Wi-Fi name and password
Use the 2.4 GHz network (see step 1).

### d) The entity IDs
Every sensor and switch in Home Assistant has an **entity ID**, such as `sensor.living_room_temperature` or
`switch.kitchen_lamp`. The screen needs the IDs of the things you want to see.

To find one:

1. Go to **Settings → Devices & services → Entities** and type in the search box (for example "temperature").
2. Click the entity, then click the **cog icon** at the top right.
3. Copy the **Entity ID** field exactly.

Make a list:

| What | Example | Where it goes |
|---|---|---|
| Inside temperature | `sensor.living_room_temperature` | `ENT_INDOOR_TEMP` |
| Outside temperature | `sensor.outdoor_temperature` | `ENT_OUTDOOR_TEMP` |
| Weather | `weather.forecast_home` | `ENT_WEATHER` |
| Thermostat | `climate.hive` | `ENT_HEATING` |
| Octopus prices, cost, kWh, demand | search `current_rate` | `ENT_RATE` … `ENT_DEMAND` |
| Each smart plug's power (watts) | `sensor.kettle_plug_power` | `PLUG1_POWER` … |
| Up to 8 switches for the Controls page | `switch.kitchen_lamp` | `TILE1_SWITCH` … |

**Octopus entities:** search for `current_rate`. You will find a name like
`sensor.octopus_energy_electricity_1a2b3c_1234567890_current_rate`. The middle part (between `electricity_` and
`_current_rate`) is unique to your meter. The other four Octopus entities use the same middle part.

✅ **Check:** your note has the IP address, the token, the Wi-Fi name and password, and a list of entity IDs.

---

## 5. Create your settings file

1. In VS Code's Explorer on the left, open the `src` folder.
2. Right-click **`panel_settings.example.h`**, choose **Copy**, then right-click the `src` folder and choose **Paste**.
   You get a file called `panel_settings.example copy.h` or similar.
3. Right-click the copy, choose **Rename**, and name it exactly **`panel_settings.h`**.
4. Open `panel_settings.h` and fill it in from your note.
   - **Only change what is inside the "quotes" and the numbers.** Keep the quotes.
   - For anything you don't have, leave the quotes empty: `""`.
5. Save with **Ctrl+S** (Windows/Linux) or **Cmd+S** (Mac).

How the main parts work:

- **Plugs (`PLUG1` to `PLUG10`):**
  - `NAME` is the label on the screen; keep it short.
  - `POWER` is the plug's watts sensor.
  - `COST` is optional (an Octopus "cost tracker" sensor for that plug).
  - The Energy page shows the 8 plugs using the most power at that moment.
- **Switches (`TILE1` to `TILE8`):**
  - `NAME` is the label on the Controls page.
  - `SWITCH` is what it turns on and off.
  - `PLUG` is the number of the plug above whose watts appear next to it, or `0` for none.
  - For something that must not go off by mistake (for example a desk with a computer), add a line such as
    `#define TILE5_CONFIRM_OFF 1`. Turning it off then needs a second tap.
  - **Don't add a fridge, freezer, router or anything else that must stay on.**
- **Prices:** `RATE_CHEAP` and `RATE_PEAK` are in pounds per kWh. The defaults suit Octopus Cosy.
- **`HIVE_BOOST`:** set it to `1` if you have Hive heating (holding the Heating card then boosts it). Otherwise set `0`.
- **Alert (optional):** a red banner when `ALERT_SWITCH` is on but its plug uses less than `ALERT_WATTS` for
  `ALERT_MINUTES`. `ALERT_PLUG` is the number (1–10) of the plug above that measures it.

Your settings stay on your computer: `panel_settings.h` is never uploaded to GitHub.

✅ **Check:** `src/panel_settings.h` exists and has your Wi-Fi name, the IP address and your token in it.

---

## 6. Load the program onto the screen

1. Plug the board into the computer with the USB-C cable.
2. In VS Code, click the **→ arrow (Upload)** in the blue bar at the bottom of the window.
   (Or click the ant-head icon, then **cyd35 → General → Upload**.)
3. The first upload takes a few minutes while it builds. Wait for the green **SUCCESS** message in the terminal panel.

If it stops with **"panel_settings.h is missing"**, redo step 5 and check the file name exactly.

If it says **"Failed to connect"**:

1. Hold the **BOOT** button on the board.
2. Tap **RESET** (or **RST**), or unplug and replug the cable while still holding BOOT.
3. Let go of BOOT and click Upload again.
4. When it finishes, press RESET once.

✅ **Check:** within about 20 seconds the screen shows the clock, and then your temperatures appear.

---

## 7. Check that it's working

1. Click the **plug icon (Serial Monitor)** in the blue bar at the bottom.
2. Every 10 seconds you'll see a line like this:
   ```
   wifi 3 ip 192.168.1.77 rssi -55 ha 1 heap ... psram ...
   ```
   - `wifi 3` means connected to Wi-Fi.
   - `ha 1` means Home Assistant is answering.
3. Lines such as `HA GET sensor.xxx -> 404` mean that entity ID is wrong. Check its spelling in `panel_settings.h`,
   then upload again.

✅ **Check:** `wifi 3` and `ha 1`. The Energy and Controls pages show your figures (tap the right edge of the
screen, or swipe, to change page).

**Using it:**

- **Change page:** tap near the left or right edge, or swipe.
- **Controls:** tap **Tap to unlock** at the bottom, then tap a switch. It locks again by itself after 25 seconds.
- **Hidden menu:** hold the page name (top left) for about a second. Use it to **rotate** the screen to fit how you
  mount it, change the **brightness**, or see **system info**. These are saved on the board.

Now unplug the board from the computer and plug it into any USB phone charger wherever you want it. It reconnects on
its own after power cuts.

---

## 8. Changing something later

Edit `src/panel_settings.h`, save it, plug the board into the computer and click **Upload (→)** again. Your settings
are kept in that file, so you only change what you need.

---

**Getting a newer version later:** download the ZIP again, unzip it, and copy your `src/panel_settings.h` into the new
folder's `src` folder before you click Upload. Check `panel_settings.example.h` for new settings and copy any new lines
into your file.

---

## 9. Troubleshooting

| What you see | What to do |
|---|---|
| No ant-head icon in VS Code | The PlatformIO IDE extension isn't installed or hasn't finished. Check Extensions, then restart VS Code. |
| "panel_settings.h is missing" | Step 5. The file must be called exactly `panel_settings.h` and be in the `src` folder. |
| Upload: "Failed to connect" / no port found | Try another USB-C cable (it may be charge-only) and a different USB port. Then try the BOOT/RESET steps in step 6. |
| Screen stays black | Press RESET. If still black, upload again and watch for errors. |
| "Connecting to Wi-Fi..." forever | Wrong Wi-Fi name or password (capitals matter), or a 5 GHz-only network. |
| Top right says **HA offline** in red | Wrong `HA_HOST` (it must be numbers like `192.168.1.50`), wrong port, or the token was copied badly or deleted. Make a new token and paste it again. |
| A value shows `--` | That entity ID is wrong, or the device is offline in Home Assistant. Check the ID in the Entities list. |
| A switch says **Unavailable** | That switch is offline in Home Assistant (for example the plug is unplugged). |
| A switch says "sending..." for a while | Normal for Bluetooth switches: they can take up to ~20–40 seconds. |
| Tapping a switch does nothing | The Controls page is locked: tap **Tap to unlock** at the bottom first. |
| The screen is sideways or upside down | Hidden menu (hold the page name) → **Rotate**. |
| The time is wrong by an hour | The `TZ_RULE` doesn't match your country. The default is UK. |
| Holding the Heating card does nothing | `HIVE_BOOST` is `0`, or your thermostat isn't Hive. |
