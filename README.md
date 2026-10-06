# 🎃 Spooky Lights — a Zigbee Halloween light for Home Assistant

An **M5Stack M5NanoC6** (ESP32-C6) becomes a Zigbee colour light with ten
Halloween effects. It drives the NanoC6's own RGB LED and/or a string of
**Grove Chainable RGB LED v2.0** modules, **counts the modules live as you plug
them in or pull them out**, and re-flows every effect to fit. A companion
**Home Assistant integration (HACS)** turns the raw Zigbee controls into a
proper light with a named effect list, a spotlight slider whose range tracks
the string length, a "LED added/removed" event, and a jump-scare button.

[![HACS Custom](https://img.shields.io/badge/HACS-Custom-41BDF5.svg)](https://hacs.xyz/)
[![Validate](https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control/actions/workflows/validate.yml/badge.svg)](https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control/actions/workflows/validate.yml)
[![Release](https://img.shields.io/github/v/release/rightiswrong/m5nanoc6-zigbee-lighting-control)](https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control/releases/latest)

---

## What's in the box

| Path | What |
|---|---|
| `firmware/NanoC6_SpookyLights/` | Arduino sketch for the NanoC6 (Zigbee **router**) |
| `custom_components/spooky_lights/` | Home Assistant integration (installed by HACS) |
| `zigbee2mqtt/nanoc6_spooky.mjs` | Optional external converter if you use Zigbee2MQTT instead of ZHA |
| `docs/WIRING.md` | Wiring, including the loopback needed for hot-plug detection |
| `docs/PUBLISHING.md` | How to get the integration into the HACS default list |
| `tools/hosttest/` | Firmware logic compiled on a PC against a bit-level P9813 chain simulator |
| `tests/` | Integration logic tests + firmware ⇄ integration sync checks |
| `.github/workflows/` | Validation on every push; `release.yml` builds and publishes binaries on a `v*` tag |

## The effects

| # | Effect | What it does |
|---|---|---|
| 0 | Solid | Your chosen colour, steady |
| 1 | Candle Flicker | Every pixel is its own flame; dims redden like embers, the odd gust nearly snuffs one out |
| 2 | Ghost Breath | A slow breath rolls down the string, peaks bleaching toward moonlit white |
| 3 | Lightning Storm | Brooding blue sky; strikes of 1–4 stutters that spill to neighbouring pixels |
| 4 | Witch's Cauldron | Churning emerald ⇄ violet brew with bubbles that swell and pop |
| 5 | Tell-Tale Heart | *lub-dub* … rippling outward from the middle of the string |
| 6 | Haunted Chase | One glowing eye with a comet tail prowls, sometimes doubling back; on a single LED it watches… and blinks |
| 7 | Poltergeist | Mostly dark; pixels get thrown into lurid colours, and now and then the whole string stutters |
| 8 | Possessed Pumpkin | A friendly orange candle — until it turns sickly green and jitters for a moment |
| 9 | Blood Moon | A slow tide of crimson, rust and near-black |

Effects 0–3 and 5–7 take their base colour from the light's colour picker.
Brightness, speed (1–100), and on/off fades apply to all of them.

---

## Quick start

### 1. Wire it
Plug Grove Chainable LEDs into the NanoC6's Grove port and daisy-chain them.
For **automatic module counting**, add the two-resistor sense tap and the
return wire described in [`docs/WIRING.md`](docs/WIRING.md). Without it,
everything still works — set the count by hand from Home Assistant and set
`CHAIN_DETECT_LOOPBACK 0` in `config.h`.

### 2. Flash the firmware

**Option A — prebuilt binary (fastest).** Download
`nanoc6-spooky-<version>-factory.bin` from the [latest release](https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control/releases/latest),
open [ESPHome Web](https://web.esphome.io) in Chrome or Edge, connect the NanoC6
by USB-C, choose **Install** and pick the file. Or from a terminal:

```bash
esptool --chip esp32c6 write_flash 0x0 nanoc6-spooky-v1.0.0-factory.bin
```

The prebuilt image uses the default `config.h` (loopback detection on,
24-module maximum, 1000 mA budget). To change those, build it yourself:

**Option B — Arduino IDE.**
1. Arduino IDE 2.x → Boards Manager → **esp32 by Espressif Systems ≥ 3.3.0**.
2. Open `firmware/NanoC6_SpookyLights/NanoC6_SpookyLights.ino`.
3. Tools menu:
   * Board: **ESP32C6 Dev Module**
   * USB CDC On Boot: **Enabled**
   * Partition Scheme: **Zigbee ZCZR 4MB with spiffs**
   * Zigbee mode: **Zigbee ZCZR (coordinator/router)**
4. Upload. Open the Serial Monitor at 115200 to watch it count modules.

It's a **router** because the NanoC6 is USB-powered: it strengthens your mesh
for battery sensors out on the porch instead of just using it.

### 3. Pair with Home Assistant (ZHA)
Settings → Devices & services → Zigbee Home Automation → **Add device**, then
power the NanoC6. The blue LED blinks while it searches and goes dark once
joined. It appears as **SpookyLab NanoC6-Spooky** with a light, a
"Grove LEDs detected" sensor and five number controls.
Already paired somewhere else? Hold the button **5 s** until the LED turns red,
then release to factory-reset Zigbee.

*Zigbee2MQTT user?* Copy `zigbee2mqtt/nanoc6_spooky.mjs` into
`<z2m data>/external_converters/` and restart Z2M before pairing.

### 4. Install the integration with HACS
Until it's in the HACS default list: HACS → ⋮ → **Custom repositories** →
`https://github.com/rightiswrong/m5nanoc6-zigbee-lighting-control`, type **Integration** → Download → restart Home Assistant.

[![Open in HACS](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=rightiswrong&repository=m5nanoc6-zigbee-lighting-control&category=integration)

Then Settings → Devices & services → **Add integration → Spooky Lights**,
pick the NanoC6, confirm the auto-mapped entities, done.

---

## What you get in Home Assistant

| Entity | From | Purpose |
|---|---|---|
| **Haunted light** | integration | On/off, brightness, colour **and a named effect list** — works in light cards, scenes, `light.turn_on` with `effect:` |
| **Effect** (select) | integration | Pick an effect by name |
| **Output** (select) | integration | Auto · Onboard only · Grove only · Onboard + Grove |
| **Spotlight pixel** (number) | integration | One pixel holds the light colour while the rest keep haunting. **Its maximum follows the live pixel count**; if the string shrinks below the chosen pixel, the spotlight switches off |
| **Active pixels** (sensor) | integration | How many pixels the effect is painting, with detected/manual/output attributes |
| **Chain change** (event) | integration | Fires `led_added` / `led_removed` with `previous`, `current`, `pixels` |
| **Jump scare** (button) | integration | 8 s of full-brightness Lightning Storm, then restores everything |
| Grove LEDs detected, Manual LED count, Effect speed, … | ZHA/Z2M | The raw firmware controls (speed lives here) |

A bus event `spooky_lights_chain_changed` is fired too, for automations that
prefer event triggers.

### Example automations

```yaml
# Doorbell → jump scare
- alias: Trick-or-treat scare
  triggers:
    - trigger: state
      entity_id: binary_sensor.front_doorbell
      to: "on"
  actions:
    - action: button.press
      target:
        entity_id: button.nanoc6_spooky_jump_scare

# Tell me when a module is unplugged (kids, cables, wind…)
- alias: Spooky string lost a light
  triggers:
    - trigger: state
      entity_id: event.nanoc6_spooky_chain_change
      attribute: event_type
      to: led_removed
  actions:
    - action: notify.mobile_app_phone
      data:
        message: >
          A Grove LED went dark: {{ trigger.to_state.attributes.previous }} →
          {{ trigger.to_state.attributes.current }} modules.

# Dusk: candles. 22:00: something else moves in.
- alias: Halloween evening
  triggers:
    - trigger: sun
      event: sunset
  actions:
    - action: light.turn_on
      target: { entity_id: light.nanoc6_spooky_haunted_light }
      data: { effect: Candle Flicker, rgb_color: [255, 90, 0], brightness: 200 }
    - delay: "03:00:00"
    - action: light.turn_on
      target: { entity_id: light.nanoc6_spooky_haunted_light }
      data: { effect: Possessed Pumpkin }
```

(Entity IDs depend on your device name — check Settings → Entities.)

---

## How it works (the reasoning, step by step)

**1 · Zigbee shape.** The Arduino-ESP32 Zigbee library gives us standard ZCL
endpoints. A *Color Dimmable Light* (endpoint 10) handles on/off, level and
colour. Everything Halloween-specific rides on generic **Analog Input/Output**
endpoints (11–15). Because those are standard clusters, ZHA creates entities
for them with no custom quirk, naming each from the `description` attribute the
firmware sets. The integration then maps those raw entities to meaningful
controls. Zigbee endpoints are fixed at pairing time, so rather than adding or
removing endpoints when the string changes, **the count is data** — reported
live on endpoint 11 — and every control that depends on it adapts.

**2 · Thread safety.** Zigbee callbacks run in the Zigbee FreeRTOS task. They
only copy values into a shared `ShowState` under a spinlock; all rendering and
I/O happens in `loop()` at 50 fps. Settings are saved to NVS 5 s after the last
change to spare the flash.

**3 · Driving the P9813.** Each module takes a 32-bit word: flag bits `11`, an
inverted checksum of the colour's top bits, then B, G, R — framed by 32 zero
bits. It's bit-banged on G1 (clock) and G2 (data) at ~150 kHz; a 24-module
frame takes ~5 ms. The end frame is padded to 64 bits so modules that re-time
the stream by one clock still latch.

**4 · Counting modules on a write-only bus.** A P9813 chain never answers, so
counting requires the last module's output to return to the controller (see
[WIRING](docs/WIRING.md)). Every 1.5 s the normal refresh becomes a *probe*:
after each clock edge the firmware briefly releases G2 (with the clock held low,
so module #1 ignores it), reads the returning line through the divider, and
drives the next bit. Each module delays the stream by one 32-bit word, so the
first `1` (a flag bit) arrives at bit `32 + 32·N + d` → **N = (i − 32) / 32**.
The probe sends real colours, so it never blinks. A new count must be seen on
two consecutive probes (contact bounce while plugging) before it's accepted.

**5 · Effects on a virtual canvas.** Effects draw into *n* virtual pixels,
where *n* comes from the output mode (onboard, chain, or onboard + chain). When
*n* changes, new pixels are "summoned" with a 1.5 s pale shimmer instead of
popping on. Then: spotlight → brightness × fade (perceptual) → gamma 2.2 →
**power limiter** (estimated mA per channel, uniform scale-down to the budget)
→ WS2812 via RMT + P9813 chain.

**6 · Home Assistant.** The integration attaches its entities to the same
device as the ZHA/Z2M ones, follows their state, and writes back through
`number.set_value` / `light.turn_on`. All the arithmetic it shares with the
firmware (effect names, output modes, pixel maths) lives in an HA-free
`logic.py`, and tests read the firmware source to make sure both sides agree.

---

## Testing

```bash
tools/hosttest/run.sh                   # firmware logic on a PC (needs g++)
python3 -m unittest discover -s tests   # integration logic + sync checks
```

The host test compiles the real sketch against API stubs and a bit-level model
of a P9813 chain wired through the loopback divider. It checks the colour
encoding against Seeed's reference, counts chains of 0–24 modules under two
different chip models, hot-plugs modules mid-show, sends HA commands, presses
the button, and checks the power limiter — under AddressSanitizer/UBSan, for
both versions of the Arduino Zigbee colour-light API. GitHub Actions also
compiles the sketch for the ESP32-C6 with Espressif's core and runs HACS and
hassfest validation.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Serial says *sense line is high during the start frame* | The 18 kΩ pull-down is missing, or the return wire is connected without the divider |
| Count stays 0 with modules attached | Terminator not on the **last** module's OUT, or `CHAIN_DETECT_LOOPBACK` is 0. Use *Manual LED count* meanwhile |
| Far modules flicker or show wrong colours | Long cables: raise `P9813_HALF_PERIOD_US` (e.g. 6–10) |
| No "Grove LEDs detected" sensor in ZHA | Update Home Assistant (generic Analog Input sensors are a recent ZHA addition); everything else works without it |
| Onboard LED stays dark | Nothing to wire — check that `Output` isn't *Grove chain only* |
| Device won't pair | Hold the button 5 s → red → release (Zigbee factory reset), then pair again |

## License

MIT — see [LICENSE](LICENSE).
