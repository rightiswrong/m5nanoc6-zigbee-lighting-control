// =============================================================================
//  Matter transport — Matter over Thread (ESP32-C6 / NanoC6), commissioned
//  over Bluetooth LE with a QR code. Works with Home Assistant, Apple Home,
//  Google Home and SmartThings; the Spooky Lights HA integration adds the
//  named effect list, spotlight slider and chain events on top.
//
//  Endpoints are numbered in creation order (the HA integration relies on it).
//  Six of them: compact enough for any Arduino Matter target (the most
//  restrictive allow 7 including the root) and light on heap:
//     1  Extended colour light ..... on/off, brightness, colour
//     2  Dimmable plug-in unit ..... effect   (off = Solid, else banded level)
//     3  Dimmable plug-in unit ..... effect speed (level ≈ speed %)
//     4  Dimmable plug-in unit ..... spotlight pixel (off = none, banded level)
//     5  Dimmable plug-in unit ..... setup: output·25 + manual LED count (fine band)
//     6  Light sensor .............. detected Grove LEDs, as count + 1 lux
//  Banded level: v ↔ 10 + 9·v.  Fine band: v ↔ 10 + 2·v.  (MatterCodes.h)
//
//  Pairing: every device makes its own random passcode + discriminator on
//  first boot (kept in NVS "spooky_mt"). Its QR code is printed on the serial
//  monitor at boot and every minute until commissioned.
// =============================================================================
#include "config.h"
#if SPOOKY_MATTER

#include <Arduino.h>
#include <Matter.h>
#include <Preferences.h>
#include <esp_random.h>
#include "Effects.h"
#include "MatterCodes.h"
#include "Show.h"
#include "Transport.h"

namespace {

MatterColorLight light;
MatterDimmablePlugin effectEp, speedEp, spotEp, setupEp;
MatterLightSensor countEp;
constexpr uint8_t MATTER_APP_ENDPOINTS = 6;   // + root = 7, the smallest Arduino Matter limit
static_assert(MAX_CHAIN < mcodes::SETUP_STRIDE, "manual count must fit the setup packing");
static_assert(FX_COUNT - 1 <= mcodes::BAND_MAX_VALUE, "effects must fit the band");

// What the controller already knows about the light (callbacks write it
// inside the g_show lock, loop() after publishing).
struct {
  bool on;
  uint8_t level;
  RGB8 color;
} g_repLight;
int g_repCount = -1;

uint32_t g_passcode = 0;
uint16_t g_discriminator = 0;
uint32_t g_lastQr = 0;
bool g_printedOnce = false;
bool g_wasCommissioned = false;

uint32_t hwRandom() { return esp_random(); }

espHsvColor_t hsvOf(const ShowState &s) {
  espRgbColor_t rgb = {s.color.r, s.color.g, s.color.b};
  espHsvColor_t hsv = espRgbColorToHsvColor(rgb);
  hsv.v = s.level;                        // Matter carries brightness as the level
  return hsv;
}

RGB8 rgbOf(espHsvColor_t hsv) {
  hsv.v = 255;                            // colour at full value; level is separate
  espRgbColor_t rgb = espHsvColorToRgbColor(hsv);
  return {rgb.r, rgb.g, rgb.b};
}

// ----------------------------------------------------------- setup codes
void loadOrCreateSetupCodes() {
  Preferences p;
  p.begin("spooky_mt", false);
  g_passcode = p.getUInt("pin", 0);
  g_discriminator = p.getUShort("disc", 0xFFFF);
  if (!mcodes::passcodeValid(g_passcode) || g_discriminator > 0x0FFF) {
    mcodes::generateSetup(hwRandom, g_passcode, g_discriminator);
    p.putUInt("pin", g_passcode);
    p.putUShort("disc", g_discriminator);
    Serial.println("[matter] generated this device's own setup code");
  }
  p.end();
}

void emitQrLine(const char *line) {
  Serial.print("      ");
  Serial.println(line);
}

void printPairingInfo() {
  String url = Matter.getOnboardingQRCodeUrl();
  String manual = Matter.getManualPairingCode();
  char payload[48] = "";
  char manualFmt[20] = "";
  mcodes::payloadFromUrl(url.c_str(), payload, sizeof(payload));
  mcodes::formatManualCode(manual.c_str(), manualFmt, sizeof(manualFmt));

  Serial.println();
  Serial.println("  ==== " BOARD_NAME " Spooky Lights — ready to pair (Matter) ====");
  Serial.println("  Home Assistant app: Settings > Devices > Add device > Add Matter device,");
  Serial.println("  then scan this code. Keep the phone near the light (pairing uses Bluetooth).");
  Serial.println();
  if (payload[0] && !mcodes::printQr(payload, MATTER_QR_DARK_TERMINAL, emitQrLine))
    Serial.println("      (QR code could not be drawn — use the codes below)");
  Serial.println();
  Serial.printf("  Manual pairing code : %s\n", manualFmt);
  Serial.printf("  QR payload          : %s\n", payload[0] ? payload : "(unavailable)");
  Serial.printf("  QR code in a browser: %s\n", url.c_str());
  Serial.println("  Printable label     : python3 tools/matter_label.py --log <this output>");
  Serial.println();
}

// ------------------------------------------------------------- callbacks
// Run in the Matter task: update g_show and return. loop() publishes the rest.
bool onLight(bool on, espHsvColor_t hsv) {
  RGB8 c = rgbOf(hsv);
  g_show.edit([&](ShowState &s) {
    s.on = g_repLight.on = on;
    s.level = g_repLight.level = hsv.v;
    s.color = g_repLight.color = c;
  });
  return true;
}

uint8_t bandValue(bool on, uint8_t level, uint8_t maxValue) {
  if (!on) return 0;
  uint8_t v = mcodes::levelToBand(level);
  return v > maxValue ? maxValue : v;
}

class MatterTransport : public Transport {
public:
  const char *name() const override { return "matter"; }

  void begin(uint8_t detected) override {
    loadOrCreateSetupCodes();
    char serial[20];
    snprintf(serial, sizeof(serial), "%012llX", (unsigned long long)ESP.getEfuseMac());
    Matter.setVendorName(ZB_MANUFACTURER);
    Matter.setProductName("Spooky Lights " BOARD_NAME);
    Matter.setDeviceName(BOARD_NAME " Spooky");
    Matter.setSerialNumber(serial);
    Matter.setHardwareVersionString(FW_VERSION);
    Matter.setSetupPasscode(g_passcode);
    Matter.setSetupDiscriminator(g_discriminator);

    // The network must be chosen before any endpoint begin().
#if MATTER_USE_WIFI && BOARD_HAS_WIFI
    if (!Matter.selectNetwork(MATTER_NETWORK_WIFI)) Serial.println("[matter] Wi-Fi not available in this build");
#else
    if (!Matter.isNetworkSupported(MATTER_NETWORK_THREAD) || !Matter.selectNetwork(MATTER_NETWORK_THREAD))
      Serial.println("[matter] this build has no Matter-over-Thread (needs Arduino-ESP32 ≥ 3.3.12)");
#endif

    const ShowState s = g_show.get();
    if (!light.begin(s.on, hsvOf(s))) Serial.println("[matter] ERROR: could not create the light endpoint");
    light.onChange(onLight);
    light.onIdentify([](bool active) {
      g_identifyUntil = active ? millis() + 60000u : 0u;
      return true;
    });
    g_repLight = {s.on, s.level, s.color};

    int ok = 1;     // the light above
    ok += effectEp.begin(s.effect != FX_SOLID, mcodes::bandToLevel(s.effect));
    effectEp.onChange([](bool on, uint8_t level) {
      // Idempotent: our own setLevel()/setOnOff() in loop() may echo back here.
      uint8_t v = bandValue(on, level, FX_COUNT - 1);
      g_show.edit([&](ShowState &st) { st.effect = v; });
      return true;
    });
    ok += speedEp.begin(true, mcodes::speedToLevel(s.speed));
    speedEp.onChange([](bool, uint8_t level) {
      uint8_t v = mcodes::levelToSpeed(level);
      g_show.edit([&](ShowState &st) { st.speed = v; });
      return true;
    });
    ok += spotEp.begin(s.spotlight != 0, mcodes::bandToLevel(s.spotlight));
    spotEp.onChange([](bool on, uint8_t level) {
      uint8_t v = bandValue(on, level, MAX_PIXELS);
      g_show.edit([&](ShowState &st) { st.spotlight = v; });
      return true;
    });
    ok += setupEp.begin(true, mcodes::fineToLevel(mcodes::packSetup(s.output, s.manualCount)));
    setupEp.onChange([](bool, uint8_t level) {
      uint8_t packed = mcodes::levelToFine(level);
      uint8_t out = mcodes::setupOutput(packed), man = mcodes::setupManual(packed);
      if (out >= OUT_COUNT) out = OUT_COUNT - 1;
      if (man > MAX_CHAIN) man = MAX_CHAIN;
      g_show.edit([&](ShowState &st) {
        st.output = out;
        st.manualCount = man;
      });
      return true;
    });
    ok += countEp.begin(mcodes::countToLux(detected));
    g_repCount = detected;
    if (ok != MATTER_APP_ENDPOINTS)
      Serial.printf("[matter] ERROR: only %d of %u endpoints could be created\n", ok, (unsigned)MATTER_APP_ENDPOINTS);

    Serial.printf("[matter] free heap before start: %u bytes\n", (unsigned)ESP.getFreeHeap());
    Matter.begin();
    g_wasCommissioned = Matter.isDeviceCommissioned();
    Serial.printf("[matter] started — %s, %s, free heap %u bytes\n",
                  Matter.getSelectedNetwork() == MATTER_NETWORK_WIFI ? "Wi-Fi" : "Thread",
                  g_wasCommissioned ? "already commissioned" : "waiting to be commissioned", (unsigned)ESP.getFreeHeap());
  }

  void loop(uint32_t now, uint8_t detected) override {
    const bool commissioned = Matter.isDeviceCommissioned();
    if (!commissioned) {
      if (g_wasCommissioned) {
        // The controller removed us. Restart so the BLE commissioning window
        // reopens cleanly (what the Arduino Matter examples do).
        Serial.println("[matter] fabric removed — restarting to accept a new pairing");
        delay(500);
        ESP.restart();
        return;
      }
      if (!g_printedOnce || now - g_lastQr >= MATTER_QR_REPEAT_MS) {
        printPairingInfo();
        g_lastQr = now;
        g_printedOnce = true;
      }
    } else if (!g_wasCommissioned) {
      Serial.printf("[matter] commissioned — welcome to the haunted house (free heap %u, lowest %u)\n",
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    }
    g_wasCommissioned = commissioned;

    // Publish local changes (button, restored settings) and normalise
    // controller writes to their canonical levels.
    ShowState s;
    bool lightDiff = false;
    g_show.peek([&](ShowState &cur) {
      s = cur;
      lightDiff = cur.on != g_repLight.on || cur.level != g_repLight.level || cur.color.r != g_repLight.color.r ||
                  cur.color.g != g_repLight.color.g || cur.color.b != g_repLight.color.b;
      g_repLight = {cur.on, cur.level, cur.color};
    });
    if (lightDiff) {
      light.setOnOff(s.on);
      light.setColorHSV(hsvOf(s));
    }
    syncBand(effectEp, s.effect);
    if (mcodes::levelToSpeed(speedEp.getLevel()) != s.speed || !speedEp.getOnOff()) {
      speedEp.setOnOff(true);
      speedEp.setLevel(mcodes::speedToLevel(s.speed));
    }
    syncBand(spotEp, s.spotlight);
    const uint8_t packed = mcodes::packSetup(s.output, s.manualCount);
    if (mcodes::levelToFine(setupEp.getLevel()) != packed || !setupEp.getOnOff()) {
      setupEp.setOnOff(true);
      setupEp.setLevel(mcodes::fineToLevel(packed));
    }
    if (detected != g_repCount) {
      countEp.setIlluminance(mcodes::countToLux(detected));
      g_repCount = detected;
    }
  }

  bool connected() override { return Matter.isDeviceCommissioned() && Matter.isDeviceConnected(); }

  void factoryReset() override {
    Serial.println("[matter] decommissioning — the light will forget its controller");
    Matter.decommission();
  }

private:
  static void syncBand(MatterDimmablePlugin &ep, uint8_t value) {
    // Done when the endpoint shows exactly the canonical encoding of value.
    const bool canonical = value == 0 ? !ep.getOnOff() : ep.getOnOff() && ep.getLevel() == mcodes::bandToLevel(value);
    if (canonical) return;
    ep.setLevel(mcodes::bandToLevel(value));
    ep.setOnOff(value != 0);
  }
};

MatterTransport g_matter;

}  // namespace

Transport &radio() { return g_matter; }

#endif  // SPOOKY_MATTER
