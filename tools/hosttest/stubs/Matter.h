// Host stand-in for the Arduino-ESP32 3.3.12 Matter library — the API surface
// the sketch uses, with the same signatures (checked against libraries/Matter
// in espressif/arduino-esp32 @ Oct 2026). Behaviour modelled on the real one:
//   * endpoints get ids 1, 2, 3… in begin() order (root is 0);
//   * setX() from the sketch updates the attribute and — like
//     esp_matter::attribute::update() — may echo into the user callback
//     (MatterStub::echo, on by default) so the sketch is tested against that;
//   * fromController() is a controller write: callback first, state only
//     changes if the callback returns true;
//   * onboarding payload / manual code are computed from the setup passcode
//     and discriminator exactly as the Matter spec describes (VID 0xFFF1,
//     PID 0x8005 like the Arduino test identity).
#pragma once
#include <stdint.h>
#include <math.h>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include "Arduino.h"

// ------------------------------------------------------------ colour types
struct RgbColor_t { uint8_t r, g, b; };
struct HsvColor_t { uint16_t h; uint8_t s; uint8_t v; };
typedef struct RgbColor_t espRgbColor_t;
typedef struct HsvColor_t espHsvColor_t;

// Same integer algorithms as cores/esp32/ColorFormat.c (hue 0..255).
inline espRgbColor_t espHsvColorToRgbColor(espHsvColor_t hsv) {
  espRgbColor_t rgb;
  if (hsv.s == 0) { rgb.r = rgb.g = rgb.b = hsv.v; return rgb; }
  uint32_t h = hsv.h, s = hsv.s, v = hsv.v;
  uint8_t region = h / 43;
  uint32_t rem = (h - region * 43) * 6;
  uint8_t p = (v * (255 - s)) >> 8, q = (v * (255 - ((s * rem) >> 8))) >> 8, t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0: rgb = {(uint8_t)v, t, p}; break;
    case 1: rgb = {q, (uint8_t)v, p}; break;
    case 2: rgb = {p, (uint8_t)v, t}; break;
    case 3: rgb = {p, q, (uint8_t)v}; break;
    case 4: rgb = {t, p, (uint8_t)v}; break;
    default: rgb = {(uint8_t)v, p, q}; break;
  }
  return rgb;
}
inline espHsvColor_t espRgbColorToHsvColor(espRgbColor_t rgb) {
  espHsvColor_t hsv;
  uint8_t mn = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b) : (rgb.g < rgb.b ? rgb.g : rgb.b);
  uint8_t mx = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) : (rgb.g > rgb.b ? rgb.g : rgb.b);
  hsv.v = mx;
  if (hsv.v == 0) { hsv.h = 0; hsv.s = 0; return hsv; }
  hsv.s = 255 * (long)(mx - mn) / hsv.v;
  if (hsv.s == 0) { hsv.h = 0; return hsv; }
  if (mx == rgb.r) hsv.h = 0 + 43 * (rgb.g - rgb.b) / (mx - mn);
  else if (mx == rgb.g) hsv.h = 85 + 43 * (rgb.b - rgb.r) / (mx - mn);
  else hsv.h = 171 + 43 * (rgb.r - rgb.g) / (mx - mn);
  hsv.h &= 0xFF;
  return hsv;
}

// ------------------------------------------------------------------- tags
struct MatterTag { uint8_t namespaceId; uint8_t tag; const char *label; };
namespace MatterTags {
namespace Switches {
static constexpr uint8_t NS = 0x43;
static constexpr uint8_t Custom = 8;
constexpr MatterTag createCustomTag(const char *label) { return MatterTag{NS, Custom, label}; }
}  // namespace Switches
}  // namespace MatterTags

namespace MatterStub {
inline bool &echo() { static bool e = true; return e; }
}

// -------------------------------------------------------------- endpoints
class MatterEndPoint {
public:
  using EndPointIdentifyCB = std::function<bool(bool)>;
  virtual ~MatterEndPoint() {}
  uint16_t getEndPointId() { return endpoint_id; }
  void onIdentify(EndPointIdentifyCB cb) { _identifyCB = cb; }
  bool setTagList(std::initializer_list<MatterTag> tags) {
    if (endpoint_id == 0 || tags.size() == 0 || tags.size() > 3) return false;
    tagList.assign(tags.begin(), tags.end());
    return true;
  }
  // test hooks
  bool identifyFromController(bool active) { return _identifyCB ? _identifyCB(active) : false; }
  std::vector<MatterTag> tagList;
  const char *kind = "";
  int reports = 0;              // attribute updates sent to controllers
protected:
  uint16_t endpoint_id = 0;
  EndPointIdentifyCB _identifyCB;
  void registerEndpoint(const char *k);
};

class MatterColorLight : public MatterEndPoint {
public:
  using EndPointCB = std::function<bool(bool, espHsvColor_t)>;
  virtual bool begin(bool initialState = false, espHsvColor_t colorHSV = {0, 254, 31}) {
    onOffState = initialState; hsv = colorHSV; registerEndpoint("color_light"); return true;
  }
  void onChange(EndPointCB cb) { _cb = cb; }
  bool setOnOff(bool on) {
    if (on == onOffState) return true;
    onOffState = on; ++reports;
    if (MatterStub::echo() && _cb) _cb(onOffState, hsv);
    return true;
  }
  bool getOnOff() { return onOffState; }
  bool setColorHSV(espHsvColor_t c) {
    if (c.h == hsv.h && c.s == hsv.s && c.v == hsv.v) return true;
    hsv = c; ++reports;
    if (MatterStub::echo() && _cb) _cb(onOffState, hsv);
    return true;
  }
  espHsvColor_t getColorHSV() { return hsv; }
  bool fromController(bool on, espHsvColor_t c) {
    if (_cb && !_cb(on, c)) return false;
    onOffState = on; hsv = c; return true;
  }
protected:
  bool onOffState = false;
  espHsvColor_t hsv = {0, 0, 0};
  EndPointCB _cb;
};

class MatterOnOffPlugin : public MatterEndPoint {
public:
  using EndPointCB = std::function<bool(bool)>;
  virtual bool begin(bool initialState = false) { onOffState = initialState; registerEndpoint("onoff_plugin"); return true; }
  void onChange(EndPointCB cb) { _cb = cb; }
  bool setOnOff(bool on) {
    if (on == onOffState) return true;
    onOffState = on; ++reports;
    if (MatterStub::echo() && _cb) _cb(onOffState);
    return true;
  }
  bool getOnOff() { return onOffState; }
  bool fromController(bool on) {
    if (_cb && !_cb(on)) return false;
    onOffState = on; return true;
  }
protected:
  bool onOffState = false;
  EndPointCB _cb;
};

class MatterDimmablePlugin : public MatterEndPoint {
public:
  using EndPointCB = std::function<bool(bool, uint8_t)>;
  virtual bool begin(bool initialState = false, uint8_t lvl = 64) {
    onOffState = initialState; level = lvl; registerEndpoint("dimmable_plugin"); return true;
  }
  void onChange(EndPointCB cb) { _cb = cb; }
  bool setOnOff(bool on) {
    if (on == onOffState) return true;
    onOffState = on; ++reports;
    if (MatterStub::echo() && _cb) _cb(onOffState, level);
    return true;
  }
  bool getOnOff() { return onOffState; }
  bool setLevel(uint8_t l) {
    if (l == level) return true;
    level = l; ++reports;
    if (MatterStub::echo() && _cb) _cb(onOffState, level);
    return true;
  }
  uint8_t getLevel() { return level; }
  bool fromController(bool on, uint8_t l) {
    if (_cb && !_cb(on, l)) return false;
    onOffState = on; level = l; return true;
  }
protected:
  bool onOffState = false;
  uint8_t level = 0;
  EndPointCB _cb;
};

class MatterLightSensor : public MatterEndPoint {
public:
  bool begin(double lux = 1.0) { registerEndpoint("light_sensor"); return setIlluminance(lux); }
  bool setIlluminance(double lux) {
    if (lux < 1.0) return false;     // the real one rejects < 1 lx
    long raw = lround(10000.0 * log10(lux) + 1);
    if (raw != rawValue) { rawValue = (uint16_t)raw; ++reports; }
    return true;
  }
  uint16_t rawValue = 0;
  // What Home Assistant shows: round(10 ** ((raw - 1) / 10000), 1)
  double haLux() const { return round(pow(10.0, (rawValue - 1) / 10000.0) * 10.0) / 10.0; }
};

// ------------------------------------------------------------------ Matter
enum matterNetwork_t { MATTER_NETWORK_NONE = 0, MATTER_NETWORK_WIFI, MATTER_NETWORK_THREAD, MATTER_NETWORK_ETHERNET };

class ArduinoMatter {
public:
  static bool setVendorName(const char *n) { st().vendor = n; return true; }
  static bool setProductName(const char *n) { st().product = n; return true; }
  static bool setDeviceName(const char *n) { st().device = n; return true; }
  static bool setSerialNumber(const char *n) { st().serial = n; return true; }
  static bool setHardwareVersionString(const char *n) { st().hw = n; return true; }
  static bool setSetupPasscode(uint32_t p) { if (st().started) return false; st().passcode = p; return true; }
  static bool setSetupDiscriminator(uint16_t d) { if (st().started || d > 0xFFF) return false; st().discriminator = d; return true; }
  static bool isNetworkSupported(matterNetwork_t n) { return n == MATTER_NETWORK_THREAD || n == MATTER_NETWORK_WIFI; }
  static bool selectNetwork(matterNetwork_t n) {
    if (!st().endpoints.empty()) return false;   // must precede every endpoint begin()
    st().network = n; return true;
  }
  static matterNetwork_t getSelectedNetwork() { return st().network; }
  static void begin() { st().started = true; }
  static bool isDeviceCommissioned() { return st().commissioned; }
  static bool isDeviceConnected() { return st().commissioned && st().online; }
  static void decommission() { st().commissioned = false; ++st().decommissions; }
  static String getOnboardingQRCodeUrl();
  static String getManualPairingCode();

  struct State {
    std::string vendor, product, device, serial, hw;
    uint32_t passcode = 20202021; uint16_t discriminator = 0xF00;
    matterNetwork_t network = MATTER_NETWORK_NONE;
    bool started = false, commissioned = false, online = false;
    int decommissions = 0;
    std::vector<MatterEndPoint *> endpoints;   // index i = endpoint id i+1
  };
  static State &st() { static State s; return s; }
};
extern ArduinoMatter Matter;

inline void MatterEndPoint::registerEndpoint(const char *k) {
  kind = k;
  ArduinoMatter::st().endpoints.push_back(this);
  endpoint_id = (uint16_t)ArduinoMatter::st().endpoints.size();
}

// --- onboarding payload (Matter Core Spec §5.1), Arduino test VID/PID
namespace MatterStubCodes {
inline std::string base38(const uint8_t *d, size_t n) {
  static const char *A = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.";
  std::string out;
  for (size_t i = 0; i < n; i += 3) {
    size_t len = n - i < 3 ? n - i : 3;
    uint32_t v = 0;
    for (size_t k = 0; k < len; ++k) v |= (uint32_t)d[i + k] << (8 * k);
    int chars = len == 3 ? 5 : len == 2 ? 4 : 2;
    for (int c = 0; c < chars; ++c) { out += A[v % 38]; v /= 38; }
  }
  return out;
}
inline int verhoeff(const std::string &s) {
  static const int D[10][10] = {{0,1,2,3,4,5,6,7,8,9},{1,2,3,4,0,6,7,8,9,5},{2,3,4,0,1,7,8,9,5,6},{3,4,0,1,2,8,9,5,6,7},{4,0,1,2,3,9,5,6,7,8},{5,9,8,7,6,0,4,3,2,1},{6,5,9,8,7,1,0,4,3,2},{7,6,5,9,8,2,1,0,4,3},{8,7,6,5,9,3,2,1,0,4},{9,8,7,6,5,4,3,2,1,0}};
  static const int P[8][10] = {{0,1,2,3,4,5,6,7,8,9},{1,5,7,6,2,8,3,0,9,4},{5,8,0,3,7,9,6,1,4,2},{8,9,1,6,0,4,3,5,2,7},{9,4,5,3,1,2,6,8,7,0},{4,2,8,6,5,7,3,9,0,1},{2,7,9,3,8,0,6,4,1,5},{7,0,4,6,9,1,3,2,5,8}};
  static const int INV[10] = {0,4,3,2,1,5,6,7,8,9};
  int c = 0;
  for (size_t i = 0; i < s.size(); ++i) c = D[c][P[(i + 1) % 8][s[s.size() - 1 - i] - '0']];
  return INV[c];
}
}  // namespace MatterStubCodes

inline String ArduinoMatter::getOnboardingQRCodeUrl() {
  if (!st().started) return String("");
  // version 3 | vid 16 | pid 16 | flow 2 | discovery 8 (BLE) | disc 12 | pin 27 | pad 4
  unsigned __int128 bits = 0; int pos = 0;
  auto put = [&](uint64_t v, int w) { bits |= (unsigned __int128)v << pos; pos += w; };
  put(0, 3); put(0xFFF1, 16); put(0x8005, 16); put(0, 2); put(0x02, 8);
  put(st().discriminator, 12); put(st().passcode, 27); pos += 4;
  uint8_t bytes[11];
  for (int i = 0; i < 11; ++i) bytes[i] = (uint8_t)(bits >> (8 * i));
  return String("https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3A" + MatterStubCodes::base38(bytes, 11));
}

inline String ArduinoMatter::getManualPairingCode() {
  if (!st().started) return String("");
  uint32_t d = st().discriminator >> 8, p = st().passcode;
  char buf[16];
  snprintf(buf, sizeof(buf), "%u%05u%04u", (unsigned)((d >> 2) & 3), (unsigned)(((d & 3) << 14) | (p & 0x3FFF)), (unsigned)(p >> 14));
  std::string s(buf);
  s += (char)('0' + MatterStubCodes::verhoeff(s));
  return String(s);
}
