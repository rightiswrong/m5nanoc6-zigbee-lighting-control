// =============================================================================
//  Board pin maps. The chip decides the board:
//    ESP32-C6 → M5Stack M5NanoC6     ESP32-H2 → M5Stack M5NanoH2
// =============================================================================
//  Sources: M5Stack docs, the core's m5stack_nanoc6 variant, and M5Unified
//  (board_M5NanoC6 / board_M5NanoH2 pin tables, Oct 2026).
//
//  Grove: on both boards M5Unified puts Port A SCL (yellow) on G1 and SDA
//  (white) on G2, i.e. CLK = G1, DATA = G2. (The NanoH2 docs page lists them
//  the other way round.) Firmware with loopback detection tries both
//  orientations and remembers the one that answers, so either is fine.
#pragma once

// The target macros come from sdkconfig.h; include it so this works in any
// translation unit, not only after Arduino.h.
#if defined(ARDUINO) && defined(__has_include)
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#endif

#if defined(CONFIG_IDF_TARGET_ESP32H2)
#define BOARD_NAME       "NanoH2"
#define PIN_GROVE_CLK    1     // yellow
#define PIN_GROVE_DATA   2     // white  (+ loopback sense)
#define PIN_STATUS_LED   4     // blue LED
#define PIN_BUTTON       9     // also BOOT strap — fine to read at runtime
#define PIN_RGB_POWER    10    // load switch for the WS2812 (must be HIGH)
#define PIN_RGB_DATA     11    // onboard WS2812
#define BOARD_HAS_WIFI   0
#elif defined(CONFIG_IDF_TARGET_ESP32C6) || !defined(ARDUINO)
#define BOARD_NAME       "NanoC6"
#define PIN_GROVE_CLK    1     // yellow (SCL)
#define PIN_GROVE_DATA   2     // white  (SDA) + loopback sense
#define PIN_STATUS_LED   7     // blue LED
#define PIN_BUTTON       9     // also BOOT strap
#define PIN_RGB_POWER    19    // load switch for the WS2812 (must be HIGH)
#define PIN_RGB_DATA     20    // onboard WS2812
#define BOARD_HAS_WIFI   1
#else
#error "Spooky Lights supports the ESP32-C6 (M5NanoC6) and ESP32-H2 (M5NanoH2)."
#endif
