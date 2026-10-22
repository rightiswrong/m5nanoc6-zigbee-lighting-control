// =============================================================================
//  MatterCodes — onboarding-code helpers and value codecs for the Matter build.
//  No Matter-library dependency, so all of it is unit-tested on the host.
// =============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace mcodes {

// ------------------------------------------------------ setup passcode / disc
// Matter forbids 00000000, 11111111 … 99999999, 12345678 and 87654321.
bool passcodeValid(uint32_t passcode);

// Draw a valid random passcode (1..99999998) and a 12-bit discriminator.
// `rnd` is any 32-bit random source (esp_random on the device).
void generateSetup(uint32_t (*rnd)(), uint32_t &passcode, uint16_t &discriminator);
// Unbiased value in [0, n), n > 0.
uint32_t uniformBelow(uint32_t (*rnd)(), uint32_t n);

// "https://…/qrcode.html?data=MT%3AY.K90…" → "MT:Y.K90…". Returns false if the
// URL carries no payload or it does not fit in `out`.
bool payloadFromUrl(const char *url, char *out, size_t outLen);

// "34970112332" → "3497-011-2332" (out needs 14 bytes). Other lengths are copied.
void formatManualCode(const char *code, char *out, size_t outLen);

// --------------------------------------------------------- value codecs
// Matter has no "number" device type, so small integer settings (output mode,
// spotlight pixel, manual LED count) ride on Dimmable Plug-in Unit endpoints as
// a LevelControl level in wide bands: value v ↔ level 10 + 9·v. A controller
// that maps levels through percentages or 0–255 brightness (Home Assistant
// does) can be off by ±1–2 levels and still decode to the right v.
// Mirrored in custom_components/spooky_lights/logic.py (tests keep them equal).
constexpr uint8_t BAND_OFFSET = 10;
constexpr uint8_t BAND_STEP = 9;
constexpr uint8_t BAND_MAX_VALUE = 26;          // 10 + 9·26 = 244 ≤ 254
uint8_t bandToLevel(uint8_t value);
uint8_t levelToBand(uint8_t level);

// The "setup" endpoint packs output mode and manual LED count into one value
// (output·25 + manual, 0..99) on a finer band: value v ↔ level 10 + 2·v.
// Home Assistant converts levels exactly (every level 1..254 survives its
// brightness round trip), and ±1 level of slop still decodes correctly.
constexpr uint8_t FINE_OFFSET = 10;
constexpr uint8_t FINE_STEP = 2;
constexpr uint8_t FINE_MAX_VALUE = 99;          // 10 + 2·99 = 208
constexpr uint8_t SETUP_STRIDE = 25;            // manual count 0..24 per output mode
uint8_t fineToLevel(uint8_t value);
uint8_t levelToFine(uint8_t level);
inline uint8_t packSetup(uint8_t output, uint8_t manual) { return (uint8_t)(output * SETUP_STRIDE + manual); }
inline uint8_t setupOutput(uint8_t packed) { return packed / SETUP_STRIDE; }
inline uint8_t setupManual(uint8_t packed) { return packed % SETUP_STRIDE; }

// Effect speed 1..100 ↔ level 1..254 (so "brightness %" ≈ speed).
uint8_t speedToLevel(uint8_t speed);
uint8_t levelToSpeed(uint8_t level);

// Detected LED count is reported as an illuminance of (count + 1) lux: Matter
// light sensors cannot report 0 lx, and lux is the one sensor unit Home
// Assistant never converts (°C/hPa would turn into °F/inHg for some users).
inline double countToLux(uint8_t count) { return (double)count + 1.0; }

// ------------------------------------------------------------------ QR printer
// Print `text` as a QR code made of Unicode half blocks, two module rows per
// text line, with a 2-module quiet zone. `darkTerminal` chooses which modules
// are drawn as blocks so the code reads dark-on-light on that background.
// Each finished line is passed to `emit` (no trailing newline).
bool printQr(const char *text, bool darkTerminal, void (*emit)(const char *line));

}  // namespace mcodes
