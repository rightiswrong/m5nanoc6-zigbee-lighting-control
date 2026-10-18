# Matter over Thread

The same firmware that speaks Zigbee can be built as a **Matter-over-Thread**
light instead, on the **M5NanoC6** (ESP32-C6).

> **NanoH2: Zigbee only.** The ESP32-H2 has the radio for Thread, but not the
> RAM for Arduino's Matter library. Starting Matter with Bluetooth pairing ran it
> out of heap at boot (versions 1.1.0 and 1.1.1). From 1.1.2 a NanoH2 Matter
> build is refused at compile time, and releases ship only its Zigbee image. Use
> a NanoC6 for Matter. You pair it by scanning a QR code, and it works with
Home Assistant, Apple Home, Google Home and SmartThings.

> **Zigbee or Matter, not both at once.** Both protocols use the same
> 802.15.4 radio, so a board runs one or the other. Flash the build you want.
> Switching later just means flashing the other `-factory.bin`.

## What you need

| | Why |
|---|---|
| A **Thread border router** on your network | Bridges the Thread mesh to your LAN. Options: Home Assistant **Connect ZBT-1 / ZBT-2** with the *OpenThread Border Router* app, a Home Assistant **Yellow**, an Apple TV 4K / HomePod mini, or a Google Nest Hub (2nd gen) / Nest Wifi Pro. |
| Home Assistant **Matter** integration | Settings → Devices & services → Add integration → *Matter* (it installs the Matter Server app). |
| Home Assistant **Thread** integration with a *preferred network* | Settings → Devices & services → *Thread*. Then in the **Companion app**: Settings → Companion app → Troubleshooting → *Sync Thread credentials*, so the phone can hand them to the light. |
| The **Home Assistant Companion app** on a phone with Bluetooth | Pairing (commissioning) happens over Bluetooth LE. |
| IPv6 enabled on Home Assistant's network interface | Matter needs it (link-local IPv6 is enough). |

> ⚠️ Your existing USB **Zigbee** stick can't do this job if it's running
> Zigbee: one radio can't be both your Zigbee coordinator and a Thread border
> router. Either add a second stick (e.g. a ZBT-2 for Thread) or use an Apple /
> Google hub you already own as the border router. Your Zigbee lights keep
> working either way.

## 1. Flash the Matter build

From the [latest release](../../../releases/latest), take
`spooky-nanoc6-matter-<version>-factory.bin`. Flash it at `0x0`
with [ESPHome Web](https://web.esphome.io) or
`esptool --chip esp32c6 write_flash 0x0 <file>`.

The factory image erases the whole flash, which is what you want the first time.
For later updates use the `-app.bin` at `0x10000`. It keeps the pairing.

**Building it yourself (Arduino IDE)**, esp32 by Espressif **3.3.12 or newer**:
Board *ESP32C6 Dev Module* · USB CDC On Boot *Enabled* ·
Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)** · Zigbee mode **Disabled** ·
Erase All Flash Before Sketch Upload **Enabled** (first time).

## 2. Get the QR code

Open a serial monitor at **115200** baud (Arduino IDE, `pio device monitor`,
or ESPHome Web's *Logs*). On boot, and every minute until it's paired, the light
prints:

```
  ==== NanoC6 Spooky Lights — ready to pair (Matter) ====
  Home Assistant app: Settings > Devices > Add device > Add Matter device,
  then scan this code. Keep the phone near the light (pairing uses Bluetooth).

      █████████████████████████████
      ██ ▄▄▄▄▄ █  ▀▀█▀▄███ ▄▄▄▄▄ ██
      ██ █   █ █ ▄█ ▄▀█▄██ █   █ ██
      …
  Manual pairing code : 2181-534-7940
  QR payload          : MT:6FCJ1MBW17KGJG03P10
  QR code in a browser: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3A6FCJ1MBW17KGJG03P10
```

**Every light has its own code.** It's generated randomly on first boot and
kept in flash, so it survives updates and re-pairing. Only *Erase All Flash*
makes a new one.

Three ways to scan it:
1. **Straight off the screen.** The block QR is drawn for dark-background
   terminals. If yours is light-themed and the phone won't read it, set
   `MATTER_QR_DARK_TERMINAL 0` in `config.h`, or use 2 or 3.
2. **The browser link.** It renders a normal QR code on any screen.
3. **A printed label** to stick on the light, its box, or inside the pumpkin:

   ```bash
   python3 tools/matter_label.py --log serial.txt --name "Porch pumpkin"
   # or: python3 tools/matter_label.py --payload MT:6FCJ1MBW17KGJG03P10
   ```

   This writes `spooky-label.svg`: 50 × 30 mm labels (several `--log` files make
   a sheet). Print at 100 %. The tool needs only Python, and it also ships in
   each release as `tools-<version>.zip`.

   ![Example label](img/matter-label.png)

You can also type the **manual pairing code** instead of scanning.

## 3. Pair it

Companion app → **Settings → Devices & services → Add device → Add Matter
device** → scan the code. Keep the phone within a few metres. The status LED
blinks until the light is paired and online, then goes dark, and the serial log
says `commissioned — welcome to the haunted house`.

It appears as **NanoC6 Spooky**, made by SpookyLab.

## 4. Add the Spooky Lights integration

Settings → Devices & services → **Add integration → Spooky Lights** → pick the
light. Matter lights are mapped automatically from their endpoint numbers;
press **Submit**. You get the same entities as with Zigbee: the haunted light
with a named effect list, Effect, Output, Effect speed, Spotlight pixel, Active
pixels, Chain change, and Jump scare. The raw helper entities (four "dimmer"
helpers and a lux sensor) are hidden from the device page. Removing the
integration shows them again.

Without the integration, everything still works. You get:
* a colour light;
* an **Effect** dimmer: off = Solid, and each step of about 3.5 % picks the next
  effect (see the table);
* dimmers for speed, spotlight and setup, plus a light sensor (see below).

## What the light exposes over Matter

Six endpoints. Each one costs memory, and the most restrictive Arduino Matter
targets allow only seven including the root, so the whole show is packed into
six.

| Endpoint | Matter device type | Meaning |
|---|---|---|
| 1 | Extended colour light | On/off, brightness, colour |
| 2 | Dimmable plug-in unit | **Effect**: off = Solid, otherwise level 10 + 9 × effect number |
| 3 | Dimmable plug-in unit | **Effect speed**: level ≈ speed % (50 % = normal) |
| 4 | Dimmable plug-in unit | **Spotlight pixel**: off = none, otherwise level 10 + 9 × pixel |
| 5 | Dimmable plug-in unit | **Setup**: level 10 + 2 × (output × 25 + manual LED count) |
| 6 | Light sensor | Grove LEDs detected, reported as **count + 1 lux** (Matter can't report 0 lx) |

Matter has no generic "number" device, so the dimmers carry small integers as
levels.
* **Effect and spotlight** use wide bands (9 levels apart), so a controller that
  converts through percentages can be a level or two off and still land right.
* **Setup** needs a finer band to fit both settings. Home Assistant converts
  levels exactly, so it's reliable there; the integration does the conversion
  for you.

| Effect level | % | Effect | | Effect level | % | Effect |
|---|---|---|---|---|---|---|
| off | — | Solid | | 64 | 25 % | Haunted Chase |
| 19 | 7 % | Candle Flicker | | 73 | 29 % | Poltergeist |
| 28 | 11 % | Ghost Breath | | 82 | 32 % | Possessed Pumpkin |
| 37 | 15 % | Lightning Storm | | 91 | 36 % | Blood Moon |
| 46 | 18 % | Witch's Cauldron | | 100 | 39 % | Demo Reel |
| 55 | 22 % | Tell-Tale Heart | | | | |

> **Updating from 1.1.0?** That version's Matter layout used 17 endpoints, and
> the endpoint numbers have changed since. Flash the 1.1.1 **`-factory.bin`**, remove the old device from
> Home Assistant, pair again, and run *Spooky Lights → Configure* to re-scan.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "Thread network not found" / pairing stalls after Bluetooth | The phone has no Thread credentials. Sync them (Companion app → Troubleshooting), and check the Thread integration shows a **preferred** network. |
| The phone never sees the light | Bluetooth on, phone close. The light only advertises while it's **not** paired. If it was paired before, hold the button 5 s (LED turns red, then release) to decommission it. |
| Paired, then "unavailable" | IPv6 or mDNS blocked between HA and the border router (VLANs, client isolation, multicast filtering). |
| Re-flashed, but it still thinks it's paired | Flashing an `-app.bin` keeps the pairing. Use the `-factory.bin`, or hold the button 5 s. |
| Serial says *this build has no Matter-over-Thread* | The Arduino core is too old. Use esp32 by Espressif ≥ 3.3.12. |
| Serial says *ERROR: only N of 6 endpoints could be created*, or `Dynamic endpoint count cannot be greater than …` | Something added endpoints beyond the library's limit. Undo the change, or send the log. |
| Crash with `Failed to allocate memory`, or `abort()` at start-up | Out of RAM. On a NanoH2 that's expected (use Zigbee). On a NanoC6, send the serial log: it prints *free heap* before and after start-up. |
| I want Matter over Wi-Fi (NanoC6 only) | Set `MATTER_USE_WIFI 1` in `config.h` and rebuild. The phone then hands over your Wi-Fi credentials instead. |
