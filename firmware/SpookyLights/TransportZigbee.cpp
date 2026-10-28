// =============================================================================
//  Zigbee transport — a Zigbee 3.0 router (or, in the battery build, an end
//  device) with standard ZCL clusters, so ZHA needs no quirk. Endpoints (unchanged since 1.0, the HA integration and the
//  Zigbee2MQTT converter rely on them):
//    10  Color Dimmable Light ... on/off, brightness, colour
//    11  Analog Input ........... detected Grove LED count (reported live)
//        Analog Output .......... manual LED count override (0 = auto-detect)
//    12  Analog Output .......... effect index (0..10)
//    13  Analog Output .......... effect speed (1..100, 50 = normal)
//    14  Analog Output .......... output routing (0 auto, 1 onboard, 2 grove, 3 both)
//    15  Analog Output .......... spotlight pixel (0 = off, 1..N)
// =============================================================================
#include "config.h"
#if SPOOKY_ZIGBEE

#include <Arduino.h>
#include "Zigbee.h"
#include "Effects.h"
#include "Show.h"
#include "Transport.h"
#include "Power.h"

namespace {

ZigbeeColorDimmableLight zbLight(EP_LIGHT);
ZigbeeAnalog zbCount(EP_COUNT);
ZigbeeAnalog zbEffect(EP_EFFECT);
ZigbeeAnalog zbSpeed(EP_SPEED);
ZigbeeAnalog zbOutput(EP_OUTPUT);
ZigbeeAnalog zbSpot(EP_SPOTLIGHT);

// What the coordinator already knows. Written by callbacks (inside the
// g_show lock) and by loop() after publishing.
ShowState g_rep;
int g_repCount = -1;
bool g_wasConnected = false;

uint8_t clampRound(float v, uint8_t lo, uint8_t hi) {
  long x = lroundf(v);
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  return (uint8_t)x;
}

// ------------------------------------------------------------ callbacks
// These run in the Zigbee task: store and return.
void onLightRgb(bool state, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
  g_show.edit([&](ShowState &s) {
    s.on = g_rep.on = state;
    s.color = g_rep.color = {r, g, b};
    s.level = g_rep.level = level;
  });
}

#define ANALOG_SETTER(fn, field, lo, hi)                                       \
  void fn(float v) {                                                           \
    uint8_t x = clampRound(v, lo, hi);                                         \
    g_show.edit([&](ShowState &s) { s.field = g_rep.field = x; });             \
  }
ANALOG_SETTER(onManualCount, manualCount, 0, MAX_CHAIN)
ANALOG_SETTER(onEffect, effect, 0, FX_COUNT - 1)
ANALOG_SETTER(onSpeed, speed, 1, 100)
ANALOG_SETTER(onOutput, output, 0, OUT_COUNT - 1)
ANALOG_SETTER(onSpotlight, spotlight, 0, MAX_PIXELS)

void onIdentify(uint16_t seconds) { g_identifyUntil = millis() + (uint32_t)seconds * 1000u; }

void configureAnalogOut(ZigbeeAnalog &ep, const char *desc, float lo, float hi, void (*cb)(float)) {
  ep.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  ep.addAnalogOutput();
  ep.setAnalogOutputDescription(desc);
  ep.setAnalogOutputResolution(1);
  ep.setAnalogOutputMinMax(lo, hi);
  ep.onAnalogOutputChange(cb);
}

void publishOut(ZigbeeAnalog &ep, uint8_t v) {
  ep.setAnalogOutput(v);
  ep.reportAnalogOutput();
}

class ZigbeeTransport : public Transport {
public:
  const char *name() const override { return "zigbee"; }

  void begin(uint8_t) override {
    zbLight.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
#ifdef ZIGBEE_COLOR_CAPABILITY_X_Y
    // core ≥ 3.3.5: explicit colour modes + mode-specific callbacks
    zbLight.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_X_Y);
    zbLight.onLightChangeRgb(onLightRgb);
#else
    zbLight.onLightChange(onLightRgb);   // older 3.x cores: same signature
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

#if SPOOKY_SLEEPY
    // End device. Its receiver stays on while awake (rx-on-when-idle), so at
    // night commands land instantly; by day the whole chip deep-sleeps.
    esp_zb_cfg_t cfg = ZIGBEE_DEFAULT_ED_CONFIG();
    if (power::timerWake()) Zigbee.setTimeout(CHECKIN_JOIN_TIMEOUT_MS);
    bool started = Zigbee.begin(&cfg, false);
    if (!started && power::timerWake()) {
      // A check-in and the network isn't answering (coordinator restarting?).
      // Don't burn the battery retrying: try again at the next check-in.
      Serial.println("[zb] no network this check-in");
      power::deepSleep(CHECKIN_INTERVAL_S);
    }
#else
    bool started = Zigbee.begin(ZIGBEE_ROUTER);
#endif
    if (!started) {
      Serial.println("[zb] failed to start — rebooting");
      delay(1000);
      ESP.restart();
    }
    Serial.println("[zb] stack started, waiting to join (put HA into pairing mode)");
  }

  void loop(uint32_t, uint8_t detected) override {
    bool connected = Zigbee.connected();
    if (connected && !g_wasConnected) {
      // Freshly joined (or rejoined): push everything so HA shows reality.
      Serial.println("[zb] joined — publishing state");
      ShowState s = g_show.get();
      zbCount.setAnalogInputReporting(0, 300, 1);
      zbLight.setLight(s.on, s.level, s.color.r, s.color.g, s.color.b);
      publishOut(zbCount, s.manualCount);
      publishOut(zbEffect, s.effect);
      publishOut(zbSpeed, s.speed);
      publishOut(zbOutput, s.output);
      publishOut(zbSpot, s.spotlight);
      g_show.peek([&](ShowState &) { g_rep = s; });
      g_repCount = -1;
    }
    g_wasConnected = connected;
    if (!connected) return;

    // Publish whatever changed locally (button, restored settings, layout).
    ShowState s, rep;
    g_show.peek([&](ShowState &cur) {
      s = cur;
      rep = g_rep;
      g_rep = cur;          // we are about to make the coordinator match
    });
    if (s.on != rep.on || s.level != rep.level || s.color.r != rep.color.r || s.color.g != rep.color.g ||
        s.color.b != rep.color.b)
      zbLight.setLight(s.on, s.level, s.color.r, s.color.g, s.color.b);
    if (s.effect != rep.effect) publishOut(zbEffect, s.effect);
    if (s.speed != rep.speed) publishOut(zbSpeed, s.speed);
    if (s.output != rep.output) publishOut(zbOutput, s.output);
    if (s.spotlight != rep.spotlight) publishOut(zbSpot, s.spotlight);
    if (s.manualCount != rep.manualCount) publishOut(zbCount, s.manualCount);

    if (detected != g_repCount) {
      zbCount.setAnalogInput(detected);
      zbCount.reportAnalogInput();
      g_repCount = detected;
    }
  }

  bool connected() override { return Zigbee.connected(); }

  void factoryReset() override {
    Serial.println("[zb] factory reset");
    Zigbee.factoryReset();   // erases network & reboots
  }
};

ZigbeeTransport g_zigbee;

}  // namespace

Transport &radio() { return g_zigbee; }

#endif  // SPOOKY_ZIGBEE
