// =============================================================================
//  Spooky Lights — a Halloween light for Home Assistant
//  M5Stack M5NanoC6 (ESP32-C6) or M5NanoH2 (ESP32-H2)
//  onboard WS2812 · Grove Chainable RGB LED v2.0 string · Zigbee or Matter/Thread
// =============================================================================
//
//  Arduino IDE (esp32 by Espressif ≥ 3.3.12), Tools menu:
//
//                     Zigbee build                   Matter-over-Thread build
//    Board .......... ESP32C6 Dev Module (NanoC6)  / ESP32H2 Dev Module (NanoH2)
//    USB CDC On Boot  Enabled                        Enabled
//    Partition ...... Zigbee ZCZR 4MB with spiffs    Huge APP (3MB No OTA/1MB SPIFFS)
//    Zigbee mode .... Zigbee ZCZR (coord./router)    Disabled
//    Erase flash .... —                              "Enabled" the first time
//
//  The Zigbee mode menu is the only switch: the same sketch becomes a Zigbee
//  router for ZHA, or a Matter-over-Thread light you pair with a QR code.
//
//  Button (G9): click = next effect · hold 1–5 s = on/off · hold 5 s+ = leave
//  the network (Zigbee reset / Matter decommission; LED turns red, release).
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "P9813Chain.h"
#include "Effects.h"
#include "Renderer.h"
#include "Show.h"
#include "Transport.h"

static P9813Chain chain;
static Renderer renderer;

static uint8_t g_detected = 0;          // debounced module count from loopback
static int g_candidate = -1;
static uint8_t g_candidateHits = 0;
static bool g_warnedStuck = false;
static bool g_swapped = false;          // Grove CLK/DATA used the other way round

static void onboardWrite(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(PIN_RGB_DATA, r, g, b);   // core 3.x RMT helper, handles GRB order
}

static uint8_t effectiveChainCount(const ShowState &s) {
  if (s.manualCount) return s.manualCount > MAX_CHAIN ? MAX_CHAIN : s.manualCount;
  return g_detected;
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
      radio().factoryReset();
    } else if (held >= 1000) {
      g_show.edit([](ShowState &s) { s.on = !s.on; });
    } else if (held >= 30) {                                // debounced click
      uint8_t fx = 0;
      g_show.edit([&](ShowState &s) {
        s.effect = (s.effect + 1) % FX_COUNT;
        s.on = true;
        fx = s.effect;
      });
      Serial.printf("[fx] %s\n", EFFECT_NAMES[fx]);
    }
    // The transport notices the change and tells the controller.
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
  }
}

static void beginChain(bool swapped) {
  g_swapped = swapped;
  if (swapped) chain.begin(PIN_GROVE_DATA, PIN_GROVE_CLK, P9813_HALF_PERIOD_US, P9813_SENSE_SETTLE_US);
  else chain.begin(PIN_GROVE_CLK, PIN_GROVE_DATA, P9813_HALF_PERIOD_US, P9813_SENSE_SETTLE_US);
}

static uint8_t bootScan() {
  g_candidate = -1;
  g_candidateHits = 0;
  for (int i = 0; i < CHAIN_DEBOUNCE_PROBES; ++i) handleProbeResult(renderer.frame(g_show.get(), 0, 1, true));
  return g_detected;
}

// Count the chain before the radio starts so the first report is right. If
// nothing answers, try the Grove pair the other way round (see board.h).
static void scanChainAtBoot() {
#if CHAIN_DETECT_LOOPBACK
  Preferences p;
  p.begin("spooky", true);
  bool swapped = p.getBool("swap", false);
  p.end();
  beginChain(swapped);
  bootScan();
#if GROVE_AUTO_ORIENT
  if (g_detected == 0) {
    beginChain(!swapped);
    if (bootScan() > 0) {
      Serial.printf("[chain] modules answer with CLK on G%d / DATA on G%d — remembering that\n",
                    g_swapped ? PIN_GROVE_DATA : PIN_GROVE_CLK, g_swapped ? PIN_GROVE_CLK : PIN_GROVE_DATA);
      p.begin("spooky", false);
      p.putBool("swap", g_swapped);
      p.end();
    } else {
      beginChain(swapped);                                  // nothing either way: keep the saved one
    }
  }
#endif
  Serial.printf("[chain] boot scan: %u Grove LED(s)\n", g_detected);
#else
  beginChain(false);
#endif
}

// ------------------------------------------------------------------ setup
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n== Spooky Lights v%s · %s · %s ==\n", FW_VERSION, BOARD_NAME, SPOOKY_MATTER ? "Matter" : "Zigbee");

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

  g_show.load();
  beginChain(false);
  renderer.begin(&chain, onboardWrite);
  scanChainAtBoot();

  radio().begin(g_detected);
}

// ------------------------------------------------------------------- loop
void loop() {
  static uint32_t lastFrame = 0, lastProbe = 0, lastBlink = 0;
  uint32_t now = millis();

  handleButton();
  radio().loop(now, g_detected);

  // Status LED: blinks until paired and online.
  if (!radio().connected()) {
    if (now - lastBlink >= 250) {
      lastBlink = now;
      digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    }
  } else {
    digitalWrite(PIN_STATUS_LED, LOW);
  }

  // Identify: violet blink while the controller asks "which one are you?"
  if ((int32_t)(g_identifyUntil - now) > 0 && ((now / 250) & 1))
    renderer.setOverride({120, 0, 255}, now + FRAME_INTERVAL_MS + 5);

  // Render
  if (now - lastFrame >= FRAME_INTERVAL_MS) {
    uint32_t dt = now - lastFrame;
    lastFrame = now;
    ShowState s = g_show.get();
    bool probe = now - lastProbe >= CHAIN_PROBE_INTERVAL_MS;
    if (probe) lastProbe = now;
    int r = renderer.frame(s, effectiveChainCount(s), dt, probe);
    if (probe) handleProbeResult(r);
    if (renderer.takeDemoSceneChanged()) {
      const DemoReel &d = renderer.demo();
      Serial.printf("[demo] %d/%d %s\n", d.segment() + 1, DemoReel::segmentCount(),
                    DemoReel::segmentAt(d.segment()).title);
    }
  }

  g_show.saveIfQuiet(now);
  delay(1);
}
