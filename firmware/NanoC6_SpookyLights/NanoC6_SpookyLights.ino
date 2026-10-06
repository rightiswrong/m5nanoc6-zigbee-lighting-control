// =============================================================================
//  NanoC6 Spooky Lights — Zigbee Halloween light for Home Assistant
//  M5Stack M5NanoC6 (ESP32-C6) · onboard WS2812 · Grove Chainable RGB LED v2.0
// =============================================================================
//
//  Arduino IDE settings (Tools menu), Arduino-ESP32 core 3.3.x or newer:
//    Board ............. "ESP32C6 Dev Module"
//    USB CDC On Boot ... Enabled            (Serial log over the USB-C port)
//    Flash Size ........ 4MB
//    Partition Scheme .. "Zigbee ZCZR 4MB with spiffs"
//    Zigbee mode ....... "Zigbee ZCZR (coordinator/router)"
//
//  Why a ROUTER and not an end device? The NanoC6 is mains (USB) powered, so it
//  can relay traffic for battery sensors around the porch — it makes your
//  Zigbee mesh stronger instead of just using it.
//
//  Zigbee endpoints (all standard ZCL clusters, so ZHA needs no quirk):
//    10  Color Dimmable Light ... on/off, brightness, colour
//    11  Analog Input ........... detected Grove LED count (reported live)
//        Analog Output .......... manual LED count override (0 = auto-detect)
//    12  Analog Output .......... effect index (0..9)
//    13  Analog Output .......... effect speed (1..100, 50 = normal)
//    14  Analog Output .......... output routing (0 auto, 1 onboard, 2 grove, 3 both)
//    15  Analog Output .......... spotlight pixel (0 = off, 1..N)
//
//  Button (G9): click = next effect · hold 1–5 s = on/off · hold 5 s+ = Zigbee
//  factory reset (LED turns red, release to confirm).
// =============================================================================

#ifndef ZIGBEE_MODE_ZCZR
#error "Select Tools > Zigbee mode > 'Zigbee ZCZR (coordinator/router)' and Partition Scheme > 'Zigbee ZCZR 4MB with spiffs'"
#endif

#include <Arduino.h>
#include <Preferences.h>
#include "Zigbee.h"
#include "config.h"
#include "P9813Chain.h"
#include "Effects.h"
#include "Renderer.h"

// ---------------------------------------------------------------- objects
static ZigbeeColorDimmableLight zbLight(EP_LIGHT);
static ZigbeeAnalog zbCount(EP_COUNT);
static ZigbeeAnalog zbEffect(EP_EFFECT);
static ZigbeeAnalog zbSpeed(EP_SPEED);
static ZigbeeAnalog zbOutput(EP_OUTPUT);
static ZigbeeAnalog zbSpot(EP_SPOTLIGHT);

static P9813Chain chain;
static Renderer renderer;
static Preferences prefs;

// Shared between the Zigbee task (callbacks) and loop(). The C6 is single-core
// but the Zigbee stack runs in its own FreeRTOS task, so guard the struct.
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static ShowState g_state;
static volatile uint32_t g_lastChangeMs = 0;
static volatile bool g_settingsDirty = false;

static uint8_t g_detected = 0;          // debounced module count from loopback
static int g_candidate = -1;
static uint8_t g_candidateHits = 0;
static bool g_countReportPending = true;
static bool g_wasConnected = false;
static bool g_warnedStuck = false;
static uint32_t g_identifyUntil = 0;

// --------------------------------------------------------------- helpers
static uint8_t clampRound(float v, uint8_t lo, uint8_t hi) {
  long x = lroundf(v);
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  return (uint8_t)x;
}

static ShowState snapshot() {
  portENTER_CRITICAL(&g_mux);
  ShowState s = g_state;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

static void touched() {
  g_lastChangeMs = millis();
  g_settingsDirty = true;
}

static void onboardWrite(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(PIN_RGB_DATA, r, g, b);   // core 3.x RMT helper, handles GRB order
}

static uint8_t effectiveChainCount(const ShowState &s) {
  if (s.manualCount) return s.manualCount > MAX_CHAIN ? MAX_CHAIN : s.manualCount;
  return g_detected;
}

// ------------------------------------------------------------ persistence
static void loadSettings() {
  prefs.begin("spooky", true);
  ShowState s;
  s.on = prefs.getBool("on", s.on);
  s.level = prefs.getUChar("lvl", s.level);
  s.color.r = prefs.getUChar("r", s.color.r);
  s.color.g = prefs.getUChar("g", s.color.g);
  s.color.b = prefs.getUChar("b", s.color.b);
  s.effect = prefs.getUChar("fx", s.effect);
  s.speed = prefs.getUChar("spd", s.speed);
  s.output = prefs.getUChar("out", s.output);
  s.spotlight = prefs.getUChar("spot", s.spotlight);
  s.manualCount = prefs.getUChar("man", s.manualCount);
  prefs.end();
  if (s.effect >= FX_COUNT) s.effect = FX_CANDLE;
  if (s.output >= OUT_COUNT) s.output = OUT_AUTO;
  if (s.speed < 1 || s.speed > 100) s.speed = 50;
  g_state = s;
}

static void saveSettingsIfQuiet() {
  if (!g_settingsDirty || millis() - g_lastChangeMs < SETTINGS_SAVE_DELAY_MS) return;
  g_settingsDirty = false;
  ShowState s = snapshot();
  prefs.begin("spooky", false);
  prefs.putBool("on", s.on);
  prefs.putUChar("lvl", s.level);
  prefs.putUChar("r", s.color.r);
  prefs.putUChar("g", s.color.g);
  prefs.putUChar("b", s.color.b);
  prefs.putUChar("fx", s.effect);
  prefs.putUChar("spd", s.speed);
  prefs.putUChar("out", s.output);
  prefs.putUChar("spot", s.spotlight);
  prefs.putUChar("man", s.manualCount);
  prefs.end();
  Serial.println("[nvs] settings saved");
}

// ----------------------------------------------------- Zigbee callbacks
// These run in the Zigbee task: store and return, rendering happens in loop().
static void onLightRgb(bool state, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  portENTER_CRITICAL(&g_mux);
  g_state.on = state;
  g_state.color = {r, g, b};
  g_state.level = level;
  portEXIT_CRITICAL(&g_mux);
  touched();
}

#define ANALOG_SETTER(fn, field, lo, hi)          \
  static void fn(float v) {                       \
    uint8_t x = clampRound(v, lo, hi);            \
    portENTER_CRITICAL(&g_mux);                   \
    g_state.field = x;                            \
    portEXIT_CRITICAL(&g_mux);                    \
    touched();                                    \
  }
ANALOG_SETTER(onManualCount, manualCount, 0, MAX_CHAIN)
ANALOG_SETTER(onEffect, effect, 0, FX_COUNT - 1)
ANALOG_SETTER(onSpeed, speed, 1, 100)
ANALOG_SETTER(onOutput, output, 0, OUT_COUNT - 1)
ANALOG_SETTER(onSpotlight, spotlight, 0, MAX_PIXELS)

static void onIdentify(uint16_t seconds) {
  g_identifyUntil = millis() + (uint32_t)seconds * 1000u;
}

// --------------------------------------------------------- Zigbee setup
static void configureAnalogOut(ZigbeeAnalog &ep, const char *desc, float lo, float hi,
                               void (*cb)(float)) {
  ep.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  ep.addAnalogOutput();
  ep.setAnalogOutputDescription(desc);
  ep.setAnalogOutputResolution(1);
  ep.setAnalogOutputMinMax(lo, hi);
  ep.onAnalogOutputChange(cb);
}

static void setupZigbee() {
  zbLight.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
#ifdef ZIGBEE_COLOR_CAPABILITY_X_Y
  // core ≥ 3.3.5: explicit colour modes + mode-specific callbacks
  zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_X_Y);
  zbLight.onLightChangeRgb(onLightRgb);
#else
  zbLight.onLightChange(onLightRgb);  // older 3.x cores: same signature
#endif
  zbLight.onIdentify(onIdentify);

  zbCount.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  zbCount.addAnalogInput();
  zbCount.setAnalogInputDescription("Grove LEDs detected");
  zbCount.setAnalogInputResolution(1);
  zbCount.setAnalogInputMinMax(0, MAX_CHAIN);
  zbCount.addAnalogOutput();
  zbCount.setAnalogOutputDescription("Manual LED count (0 = auto)");
  zbCount.setAnalogOutputResolution(1);
  zbCount.setAnalogOutputMinMax(0, MAX_CHAIN);
  zbCount.onAnalogOutputChange(onManualCount);

  configureAnalogOut(zbEffect, "Effect", 0, FX_COUNT - 1, onEffect);
  configureAnalogOut(zbSpeed, "Effect speed", 1, 100, onSpeed);
  configureAnalogOut(zbOutput, "Output (0 auto,1 onboard,2 grove,3 both)", 0, OUT_COUNT - 1, onOutput);
  configureAnalogOut(zbSpot, "Spotlight pixel (0 = off)", 0, MAX_PIXELS, onSpotlight);

  Zigbee.addEndpoint(&zbLight);
  Zigbee.addEndpoint(&zbCount);
  Zigbee.addEndpoint(&zbEffect);
  Zigbee.addEndpoint(&zbSpeed);
  Zigbee.addEndpoint(&zbOutput);
  Zigbee.addEndpoint(&zbSpot);

  if (!Zigbee.begin(ZIGBEE_ROUTER)) {
    Serial.println("[zb] failed to start — rebooting");
    delay(1000);
    ESP.restart();
  }
  Serial.println("[zb] stack started, waiting to join (put HA into pairing mode)");
}

// Push the restored state to the coordinator so HA shows reality after a reboot.
static void publishAll() {
  ShowState s = snapshot();
  zbLight.setLight(s.on, s.level, s.color.r, s.color.g, s.color.b);
  zbCount.setAnalogInputReporting(0, 300, 1);
  zbCount.setAnalogOutput(s.manualCount);
  zbEffect.setAnalogOutput(s.effect);
  zbSpeed.setAnalogOutput(s.speed);
  zbOutput.setAnalogOutput(s.output);
  zbSpot.setAnalogOutput(s.spotlight);
  zbCount.reportAnalogOutput();
  zbEffect.reportAnalogOutput();
  zbSpeed.reportAnalogOutput();
  zbOutput.reportAnalogOutput();
  zbSpot.reportAnalogOutput();
  g_countReportPending = true;
}

// ------------------------------------------------------------- the button
static void handleButton() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  bool down = digitalRead(PIN_BUTTON) == LOW;
  uint32_t now = millis();

  if (down && !wasDown) downAt = now;
  if (down && now - downAt >= BUTTON_FACTORY_RESET_MS)
    renderer.setOverride({180, 0, 0}, now + 100);          // red = "let go to reset"

  if (!down && wasDown) {
    uint32_t held = now - downAt;
    if (held >= BUTTON_FACTORY_RESET_MS) {
      Serial.println("[zb] factory reset");
      Zigbee.factoryReset();                               // erases network & reboots
    } else if (held >= 1000) {
      portENTER_CRITICAL(&g_mux);
      g_state.on = !g_state.on;
      bool on = g_state.on;
      portEXIT_CRITICAL(&g_mux);
      touched();
      if (Zigbee.connected()) zbLight.setLightState(on);
    } else if (held >= 30) {                                // debounced click
      portENTER_CRITICAL(&g_mux);
      g_state.effect = (g_state.effect + 1) % FX_COUNT;
      g_state.on = true;
      uint8_t fx = g_state.effect;
      portEXIT_CRITICAL(&g_mux);
      touched();
      Serial.printf("[fx] %s\n", EFFECT_NAMES[fx]);
      if (Zigbee.connected()) {
        zbEffect.setAnalogOutput(fx);
        zbEffect.reportAnalogOutput();
        zbLight.setLightState(true);
      }
    }
  }
  wasDown = down;
}

// -------------------------------------------------- chain count bookkeeping
static void handleProbeResult(int r) {
  if (r == P9813Chain::PROBE_LINE_STUCK_HIGH) {
    if (!g_warnedStuck) {
      Serial.println("[chain] sense line is high during the start frame — check the "
                     "loopback divider (10k series + 18k to GND, docs/WIRING.md)");
      g_warnedStuck = true;
    }
    return;
  }
  if (r < 0) return;
  if (r != g_candidate) {
    g_candidate = r;
    g_candidateHits = 1;
  } else if (g_candidateHits < 255) {
    ++g_candidateHits;
  }
  if (g_candidateHits >= CHAIN_DEBOUNCE_PROBES && r != g_detected) {
    Serial.printf("[chain] %u -> %d Grove LED%s %s\n", g_detected, r, r == 1 ? "" : "s",
                  r > g_detected ? "(welcome, new spirit)" : "(one has departed)");
    g_detected = (uint8_t)r;
    g_countReportPending = true;
  }
}

// ------------------------------------------------------------------ setup
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n== NanoC6 Spooky Lights v" FW_VERSION " ==");

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  // Onboard WS2812 sits behind a load switch. Its slew-rate-limited turn-on
  // takes ~0.5 ms and the first frame after power-up is lost, so wait and
  // send a throw-away frame.
  pinMode(PIN_RGB_POWER, OUTPUT);
  digitalWrite(PIN_RGB_POWER, HIGH);
  delay(2);
  rgbLedWrite(PIN_RGB_DATA, 0, 0, 0);

  chain.begin(PIN_GROVE_CLK, PIN_GROVE_DATA, P9813_HALF_PERIOD_US, P9813_SENSE_SETTLE_US);
  renderer.begin(&chain, onboardWrite);
  loadSettings();

#if CHAIN_DETECT_LOOPBACK
  // Count the chain before Zigbee starts so the first report is correct.
  for (int i = 0; i < CHAIN_DEBOUNCE_PROBES; ++i) handleProbeResult(renderer.frame(snapshot(), 0, 1, true));
  Serial.printf("[chain] boot scan: %u Grove LED(s)\n", g_detected);
#endif

  setupZigbee();
}

// ------------------------------------------------------------------- loop
void loop() {
  static uint32_t lastFrame = 0, lastProbe = 0, lastBlink = 0;
  uint32_t now = millis();

  handleButton();

  // Zigbee connection state → status LED + resync
  bool connected = Zigbee.connected();
  if (connected && !g_wasConnected) {
    Serial.println("[zb] joined — publishing state");
    publishAll();
  }
  g_wasConnected = connected;
  if (!connected && now - lastBlink >= 250) {
    lastBlink = now;
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
  } else if (connected) {
    digitalWrite(PIN_STATUS_LED, LOW);
  }

  // Identify: violet blink while the coordinator asks "which one are you?"
  if ((int32_t)(g_identifyUntil - now) > 0 && ((now / 250) & 1))
    renderer.setOverride({120, 0, 255}, now + FRAME_INTERVAL_MS + 5);

  // Render
  if (now - lastFrame >= FRAME_INTERVAL_MS) {
    uint32_t dt = now - lastFrame;
    lastFrame = now;
    ShowState s = snapshot();
    bool probe = now - lastProbe >= CHAIN_PROBE_INTERVAL_MS;
    if (probe) lastProbe = now;
    int r = renderer.frame(s, effectiveChainCount(s), dt, probe);
    if (probe) handleProbeResult(r);
  }

  // Tell Home Assistant about a changed chain
  if (connected && g_countReportPending) {
    zbCount.setAnalogInput(g_detected);
    zbCount.reportAnalogInput();
    g_countReportPending = false;
  }

  saveSettingsIfQuiet();
  delay(1);
}
