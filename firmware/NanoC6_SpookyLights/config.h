// =============================================================================
//  NanoC6 Spooky Lights — hardware & behaviour configuration
//  Target: M5Stack M5NanoC6 (ESP32-C6), Arduino-ESP32 core 3.x
// =============================================================================
#pragma once
#include <stdint.h>

// Keep in sync with custom_components/spooky_lights/manifest.json "version"
// (the release workflow refuses to publish if they differ from the tag).
#define FW_VERSION "1.0.0"

// ---------------------------------------------------------------- Board pins
// M5NanoC6 pin map (M5Stack docs / ESPHome devices DB):
//   G1  Grove yellow wire (labelled SCL)  -> P9813 CI  (clock in)
//   G2  Grove white  wire (labelled SDA)  -> P9813 DI  (data in) + loopback sense
//   G7  blue status LED
//   G9  user button (active low, also the BOOT strap — fine to read at runtime)
//   G19 power switch for the onboard WS2812 (must be HIGH or the LED stays dark)
//   G20 onboard WS2812 data
#define PIN_GROVE_CLK        1
#define PIN_GROVE_DATA       2
#define PIN_STATUS_LED       7
#define PIN_BUTTON           9
#define PIN_RGB_POWER        19
#define PIN_RGB_DATA         20

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
#define ZB_MODEL             "NanoC6-Spooky"

#define EP_LIGHT             10   // Color dimmable light (on/off, level, color)
#define EP_COUNT             11   // Analog In: detected LEDs / Analog Out: manual count
#define EP_EFFECT            12   // Analog Out: effect index
#define EP_SPEED             13   // Analog Out: effect speed 1..100
#define EP_OUTPUT            14   // Analog Out: output routing mode
#define EP_SPOTLIGHT         15   // Analog Out: spotlight pixel (0 = off)

// ------------------------------------------------------------------ Button
#define BUTTON_FACTORY_RESET_MS 5000
#define SETTINGS_SAVE_DELAY_MS  5000  // debounce NVS writes (flash wear)
