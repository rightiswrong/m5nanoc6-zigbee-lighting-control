// Host stand-in mirroring the Arduino-ESP32 3.3.x Zigbee API signatures used
// by the sketch (per docs.espressif.com/projects/arduino-esp32 Zigbee pages).
// Records calls so tests can check what would be reported to Home Assistant.
#pragma once
#include <stdint.h>
#include <vector>

typedef enum { ZIGBEE_COORDINATOR = 0, ZIGBEE_ROUTER = 1, ZIGBEE_END_DEVICE = 2 } zigbee_role_t;

class ZigbeeEP {
public:
  explicit ZigbeeEP(uint8_t ep) : endpoint(ep) {}
  virtual ~ZigbeeEP() {}
  bool setManufacturerAndModel(const char *m, const char *mo) { mfg = m; model = mo; return true; }
  void onIdentify(void (*cb)(uint16_t)) { identifyCb = cb; }
  uint8_t endpoint;
  const char *mfg = nullptr, *model = nullptr;
  void (*identifyCb)(uint16_t) = nullptr;
};

class ZigbeeColorDimmableLight : public ZigbeeEP {
public:
  using RgbCb = void (*)(bool, uint8_t, uint8_t, uint8_t, uint8_t);
  explicit ZigbeeColorDimmableLight(uint8_t ep) : ZigbeeEP(ep) {}
#ifdef HOST_NEW_COLOR_API
  bool setLightColorCapabilities(uint16_t c) { caps = c; return true; }
  void onLightChangeRgb(RgbCb cb) { rgbCb = cb; }
#endif
  void onLightChange(RgbCb cb) { rgbCb = cb; }
  bool setLight(bool s, uint8_t l, uint8_t r, uint8_t g, uint8_t b) {
    state = s; level = l; red = r; green = g; blue = b;
    if (rgbCb) rgbCb(s, r, g, b, l);
    return true;
  }
  bool setLightState(bool s) { state = s; return true; }
  // test hook: simulate HA sending a command
  void fromCoordinator(bool s, uint8_t l, uint8_t r, uint8_t g, uint8_t b) { if (rgbCb) rgbCb(s, r, g, b, l); }
  RgbCb rgbCb = nullptr;
  uint16_t caps = 0;
  bool state = false; uint8_t level = 0, red = 0, green = 0, blue = 0;
};
#ifdef HOST_NEW_COLOR_API
#define ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION 0x01
#define ZIGBEE_COLOR_CAPABILITY_X_Y 0x08
#define ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP 0x10
#endif

class ZigbeeAnalog : public ZigbeeEP {
public:
  explicit ZigbeeAnalog(uint8_t ep) : ZigbeeEP(ep) {}
  bool addAnalogInput() { hasIn = true; return true; }
  bool addAnalogOutput() { hasOut = true; return true; }
  bool setAnalogInputDescription(const char *d) { inDesc = d; return true; }
  bool setAnalogInputResolution(float) { return true; }
  bool setAnalogInputMinMax(float lo, float hi) { inMin = lo; inMax = hi; return true; }
  bool setAnalogInput(float v) { in = v; return true; }
  bool setAnalogInputReporting(uint16_t, uint16_t, float) { return true; }
  bool reportAnalogInput() { reportedIn.push_back(in); return true; }
  bool setAnalogOutputDescription(const char *d) { outDesc = d; return true; }
  bool setAnalogOutputResolution(float) { return true; }
  bool setAnalogOutputMinMax(float lo, float hi) { outMin = lo; outMax = hi; return true; }
  bool setAnalogOutput(float v) { out = v; return true; }
  float getAnalogOutput() { return out; }
  bool reportAnalogOutput() { reportedOut.push_back(out); return true; }
  void onAnalogOutputChange(void (*cb)(float)) { outCb = cb; }
  void fromCoordinator(float v) { out = v; if (outCb) outCb(v); }
  bool hasIn = false, hasOut = false;
  const char *inDesc = nullptr, *outDesc = nullptr;
  float in = 0, out = 0, inMin = 0, inMax = 0, outMin = 0, outMax = 0;
  std::vector<float> reportedIn, reportedOut;
  void (*outCb)(float) = nullptr;
};

class ZigbeeCore {
public:
  bool addEndpoint(ZigbeeEP *ep) { eps.push_back(ep); return true; }
  bool begin(zigbee_role_t r, bool = false) { role = r; started = true; return true; }
  bool connected() { return isConnected; }
  void factoryReset(bool = true) { resets++; }
  std::vector<ZigbeeEP *> eps;
  zigbee_role_t role = ZIGBEE_END_DEVICE;
  bool started = false, isConnected = false;
  int resets = 0;
};
extern ZigbeeCore Zigbee;
