// =============================================================================
//  Spooky Lights — behaviour configuration
//  Targets: M5Stack M5NanoC6 (ESP32-C6) and M5NanoH2 (ESP32-H2),
//           Arduino-ESP32 core 3.3.12 or newer
// =============================================================================
#pragma once
#include <stdint.h>
#include "board.h"

// Keep in sync with custom_components/spooky_lights/manifest.json "version"
// (the release workflow refuses to publish if they differ from the tag).
#define FW_VERSION "1.2.0"

// ------------------------------------------------------------ Radio stack
// Chosen with the Arduino IDE "Zigbee Mode" menu — no code changes needed:
//   Zigbee ZCZR (coordinator/router) → Zigbee router, pairs with ZHA / Z2M.
//                                      Radio listens 24/7 (mains power).
//   Zigbee ED (end device)           → Battery build: a Zigbee end device that
//                                      deep-sleeps while the light is off and
//                                      wakes every CHECKIN_INTERVAL_S to check in.
//   Disabled                         → Matter over Thread (QR-code pairing; NanoC6 only)
#if defined(ZIGBEE_MODE_ZCZR)
#define SPOOKY_ZIGBEE 1
#define SPOOKY_MATTER 0
#define SPOOKY_SLEEPY 0
#elif defined(ZIGBEE_MODE_ED)
#define SPOOKY_ZIGBEE 1
#define SPOOKY_MATTER 0
#define SPOOKY_SLEEPY 1
#else
#define SPOOKY_ZIGBEE 0
#define SPOOKY_MATTER 1
#define SPOOKY_SLEEPY 0
#endif

// Matter is NanoC6-only. On the ESP32-H2, Arduino's prebuilt Matter library
// leaves ~69 KB of heap, and Matter + Bluetooth pairing + Thread need more:
// v1.1.1 aborted at start-up with the heap exhausted (newlib lock_init_generic,
// decoded from the crash). Espressif's RAM guide gets an H2 there only with
// ESP-IDF settings Arduino doesn't expose. The NanoH2 runs the Zigbee build.
#if SPOOKY_MATTER && defined(CONFIG_IDF_TARGET_ESP32H2)
#error "The ESP32-H2 (NanoH2) doesn't have enough RAM for Arduino's Matter library. Set Tools > Zigbee mode > 'Zigbee ZCZR (coordinator/router)' and Partition Scheme > 'Zigbee ZCZR 4MB with spiffs'. Matter is supported on the NanoC6."
#endif

// ---------------------------------------------------------- Grove P9813 chain
// Hard upper bound on chained modules. Each module can draw ~60 mA at full
// white, so 24 modules is already ~1.4 A before the power limiter kicks in.
#define MAX_CHAIN            24

// Half clock period in microseconds for normal frames. The P9813 is good to
// several MHz, but long hand-made Halloween cable runs are not; 3 us (~150 kHz)
// is a calm, reliable default. Raise it if far-away modules flicker.
#define P9813_HALF_PERIOD_US 3

// How long to let the loopback line settle before sampling it (RC of the
// divider plus the return wire). 6 us is generous for ~3 m of return wire.
#define P9813_SENSE_SETTLE_US 6

// Chain hot-plug detection
//   1 = LOOPBACK : the last module's OUT data line is wired back to G2 through
//                  a divider (see docs/WIRING.md). Firmware counts modules live.
//   0 = OFF      : no return wire; use the "manual count" control from Home
//                  Assistant instead.
#define CHAIN_DETECT_LOOPBACK 1

// With loopback detection, a boot scan that finds nothing tries the Grove pins
// the other way round (CLK on white, DATA on yellow) and keeps whichever
// orientation answers. Covers boards documented with the pair swapped.
#define GROVE_AUTO_ORIENT 1

// Probe the chain this often (ms). Each probe also refreshes the LEDs, so it
// never causes a visible blink.
#define CHAIN_PROBE_INTERVAL_MS 1500

// A new count must be seen this many probes in a row before it is accepted —
// plugging a Grove cable in makes contact bounce for a moment.
#define CHAIN_DEBOUNCE_PROBES 2

// ---------------------------------------------------------------- Rendering
#define FRAME_INTERVAL_MS    20      // 50 fps
#define FADE_MS              450     // on/off fade length
#define GAMMA                2.2f    // perceptual gamma for smooth dim fades

// Rough supply budget for the LEDs. Each fully-lit channel is estimated at
// 20 mA (P9813 modules) / 12 mA (onboard WS2812). Frames that would exceed the
// budget are scaled down uniformly so the NanoC6 never browns out.
#define POWER_BUDGET_MA      1000
#define MA_PER_CHAIN_CHANNEL 20
#define MA_PER_ONBOARD_CHANNEL 12

// ------------------------------------------------------------------- Zigbee
#define ZB_MANUFACTURER      "SpookyLab"
#define ZB_MODEL             BOARD_NAME "-Spooky"     // NanoC6-Spooky / NanoH2-Spooky

#define EP_LIGHT             10   // Color dimmable light (on/off, level, color)
#define EP_COUNT             11   // Analog In: detected LEDs / Analog Out: manual count
#define EP_EFFECT            12   // Analog Out: effect index
#define EP_SPEED             13   // Analog Out: effect speed 1..100
#define EP_OUTPUT            14   // Analog Out: output routing mode
#define EP_SPOTLIGHT         15   // Analog Out: spotlight pixel (0 = off)

// ------------------------------------------------------------------- Matter
// Matter endpoints are numbered by creation order (1 = light, see
// TransportMatter.cpp). The Home Assistant integration maps them by number.
//
// Network: Thread by default. On the ESP32-C6 (which also has Wi-Fi) set this
// to 1 for Matter over Wi-Fi instead: no Thread border router needed, the
// phone hands over your 2.4 GHz Wi-Fi. Releases ship both NanoC6 builds
// (CI passes -DMATTER_USE_WIFI=1 for "nanoc6-matter-wifi").
#ifndef MATTER_USE_WIFI
#define MATTER_USE_WIFI      0
#endif

// Every device generates its own random setup passcode and discriminator on
// first boot and keeps them in flash (they survive re-pairing and firmware
// updates; "Erase All Flash" makes new ones). The QR code is printed on the
// serial monitor at boot and every MATTER_QR_REPEAT_MS until commissioned.
#define MATTER_QR_REPEAT_MS  60000

// 1 = throw the pairing code away whenever the light is decommissioned (button
// held 5 s, or removed by its controller); a fresh one is made on the next
// boot. A leaked code (photographed label, a log posted online) then stops
// working, but printed labels go stale: print a new one after each reset.
#ifndef MATTER_NEW_CODE_ON_DECOMMISSION
#define MATTER_NEW_CODE_ON_DECOMMISSION 0
#endif

// Serial QR style: 1 = for dark-background terminals (most terminals,
// Arduino IDE dark theme), 0 = for light-background serial monitors.
#define MATTER_QR_DARK_TERMINAL 1

// ------------------------------------------------------------------- Power
// CPU clock while awake. The light needs very little: an effect frame is a few
// hundred microseconds of maths every 20 ms, and the Zigbee radio is happy at
// 80 MHz. Halving the ESP32-C6's default 160 MHz trims a few mA around the
// clock. The receiver, not the CPU, is the big constant draw (~70 mA while
// listening), which is why the battery build sleeps instead.
// 0 = leave the core's default. Matter builds keep the default (its pairing
// crypto is slow enough already); the NanoH2 keeps its default 96 MHz.
#if SPOOKY_ZIGBEE
#define CPU_MHZ              BOARD_LOW_CPU_MHZ
#else
#define CPU_MHZ              0
#endif

// Battery build only (Tools > Zigbee mode > "Zigbee ED"):
// While the light is off it sleeps, waking every CHECKIN_INTERVAL_S to rejoin,
// report, and listen for CHECKIN_LISTEN_MS. Commands sent while it sleeps are
// lost by the network, so the Home Assistant integration repeats them until the
// light answers (see "Battery power" in the README). Longer interval = longer
// battery life but a slower response by day (worst case ≈ interval + ~10 s).
#define CHECKIN_INTERVAL_S   120
#define CHECKIN_LISTEN_MS    6000     // awake & listening after each rejoin
#define CHECKIN_JOIN_TIMEOUT_MS 15000 // coordinator not answering → sleep again
// After the light is switched off (or on a fresh boot), stay awake this long
// before the first sleep, so a mistaken "off" can be undone straight away.
#define SLEEP_AFTER_OFF_MS   60000

// ------------------------------------------------------------------ Button
#define BUTTON_FACTORY_RESET_MS 5000
#define SETTINGS_SAVE_DELAY_MS  5000  // save once settings have been quiet this long…
#define SETTINGS_SAVE_MIN_INTERVAL_MS 60000  // …but at most once a minute (flash wear,
                                      // even with a chatty automation)
