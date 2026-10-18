// Host tests for the Spooky Lights firmware (both boards, both radio stacks).
// Build & run: tools/hosttest/run.sh   (builds one binary per variant)
//
// Variant macros: ZIGBEE_MODE_ZCZR → Zigbee transport, otherwise Matter;
// CONFIG_IDF_TARGET_ESP32H2 → NanoH2 pin map, otherwise NanoC6.
#define SIM_IMPLEMENTATION
#include "sim.h"
#if defined(ZIGBEE_MODE_ZCZR)
#include "Zigbee.h"
ZigbeeCore Zigbee;
#else
#include "Matter.h"
ArduinoMatter Matter;
#endif

// Pull the real sketch into this translation unit (its statics become visible).
#include "../../firmware/SpookyLights/SpookyLights.ino"
#include "../../firmware/SpookyLights/MatterCodes.h"
#include "../../firmware/SpookyLights/Demo.h"

#include <random>
#include <cstdlib>
#include <string>
#include <fstream>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (cond) { ++g_pass; }                                       \
    else { ++g_fail; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
           printf(__VA_ARGS__); printf("\n"); }                   \
  } while (0)

// Reference: Seeed ChainableLED::sendColor prefix logic
static uint32_t seeedReference(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t prefix = 0xC0;
  if ((b & 0x80) == 0) prefix |= 0x20;
  if ((b & 0x40) == 0) prefix |= 0x10;
  if ((g & 0x80) == 0) prefix |= 0x08;
  if ((g & 0x40) == 0) prefix |= 0x04;
  if ((r & 0x80) == 0) prefix |= 0x02;
  if ((r & 0x40) == 0) prefix |= 0x01;
  return ((uint32_t)prefix << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static void testEncode() {
  printf("[encode] P9813 word matches Seeed reference for 20k random colours\n");
  std::mt19937 rng(31);
  int bad = 0;
  for (int i = 0; i < 20000; ++i) {
    RGB8 c{(uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng()};
    if (P9813Chain::encode(c) != seeedReference(c.r, c.g, c.b)) ++bad;
  }
  CHECK(bad == 0, "%d mismatches", bad);
}

static void testProbeCounts() {
  printf("[probe] counting N = 0..%d modules, two chip models, latency 0/1\n", MAX_CHAIN);
  P9813Chain ch;
  ch.begin(PIN_GROVE_CLK, PIN_GROVE_DATA, 3, 6);
  RGB8 px[MAX_CHAIN];
  for (int i = 0; i < MAX_CHAIN; ++i) px[i] = {(uint8_t)(10 * i + 1), (uint8_t)(200 - 3 * i), (uint8_t)(i * 7)};
  for (ChipModel m : {ChipModel::Consume, ChipModel::ShiftRegister})
    for (int lat = 0; lat <= 1; ++lat)
      for (int n = 0; n <= MAX_CHAIN; ++n) {
        sim.chips.clear();
        sim.setChain(n, m, lat);
        int got = ch.probe(px, MAX_CHAIN, MAX_CHAIN + 1);
        int got2 = ch.probe(px, MAX_CHAIN, MAX_CHAIN + 1);   // steady state
        CHECK(got == n && got2 == n, "model=%d lat=%d n=%d → %d/%d", (int)m, lat, n, got, got2);
        if (m == ChipModel::Consume) {
          int wrong = 0;
          for (int i = 0; i < n; ++i)
            if (sim.chips[i].shown.r != px[i].r || sim.chips[i].shown.g != px[i].g || sim.chips[i].shown.b != px[i].b) ++wrong;
          CHECK(wrong == 0, "n=%d: %d modules show the wrong colour after a probe", n, wrong);
        }
      }
  // Plain write() delivers colours too
  sim.chips.clear();
  sim.setChain(5);
  ch.write(px, 5);
  int wrong = 0;
  for (int i = 0; i < 5; ++i) if (sim.chips[i].shown.r != px[i].r) ++wrong;
  CHECK(wrong == 0, "write(): %d wrong", wrong);

  printf("[probe] no loopback wire → 0, miswired (stuck high) → error\n");
  sim.loopback = false;
  sim.setChain(4);
  CHECK(ch.probe(px, MAX_CHAIN, MAX_CHAIN + 1) == 0, "no loopback should read 0");
  sim.loopback = true;
  sim.stuckHigh = true;
  CHECK(ch.probe(px, MAX_CHAIN, MAX_CHAIN + 1) == P9813Chain::PROBE_LINE_STUCK_HIGH, "stuck high not flagged");
  sim.stuckHigh = false;
}

static void testProbeTiming() {
  P9813Chain ch;
  ch.begin(PIN_GROVE_CLK, PIN_GROVE_DATA, P9813_HALF_PERIOD_US, P9813_SENSE_SETTLE_US);
  RGB8 px[MAX_CHAIN] = {};
  for (int n : {0, 3, MAX_CHAIN}) {
    sim.setChain(n);
    uint64_t t0 = sim.nowUs;
    ch.probe(px, MAX_CHAIN, MAX_CHAIN + 1);
    uint64_t probeUs = sim.nowUs - t0;
    t0 = sim.nowUs;
    ch.write(px, n);
    uint64_t writeUs = sim.nowUs - t0;
    printf("[timing] n=%2d  probe ≈ %5.2f ms   normal frame ≈ %5.2f ms (delay time only)\n", n,
           probeUs / 1000.0, writeUs / 1000.0);
    CHECK(probeUs < FRAME_INTERVAL_MS * 1000u, "probe must fit in one frame slot: %llu us", (unsigned long long)probeUs);
    CHECK(writeUs < FRAME_INTERVAL_MS * 1000 / 2, "frame too slow: %llu us", (unsigned long long)writeUs);
  }
}

static void testEffects() {
  printf("[effects] %d effects × sizes × speeds: finite, bounded, alive\n", FX_PLAIN_COUNT);
  Effects fx;
  fx.begin(1234);
  RGBf out[MAX_PIXELS];
  for (int e = 0; e < FX_PLAIN_COUNT; ++e)
    for (int n : {1, 2, 7, MAX_PIXELS})
      for (int sp : {1, 50, 100}) {
        float lo = 1e9f, hi = -1e9f, sum = 0, var = 0;
        bool finite = true;
        float prev0 = -1;
        int changes = 0;
        for (int f = 0; f < 1500; ++f) {                    // 30 s of show
          fx.render(e, out, n, 20, sp, {1.0f, 0.35f, 0.0f});
          for (int i = 0; i < n; ++i) {
            for (float v : {out[i].r, out[i].g, out[i].b}) {
              if (!std::isfinite(v)) finite = false;
              lo = fminf(lo, v); hi = fmaxf(hi, v); sum += v;
            }
          }
          float v0 = 0;
          for (int i = 0; i < n; ++i) v0 += out[i].r + out[i].g + out[i].b;
          if (prev0 >= 0 && fabsf(v0 - prev0) > 1e-4f) ++changes;
          prev0 = v0;
        }
        (void)var; (void)sum;
        CHECK(finite, "%s n=%d speed=%d produced NaN/inf", EFFECT_NAMES[e], n, sp);
        CHECK(lo >= -1e-4f && hi <= 1.5f, "%s n=%d speed=%d range [%f, %f]", EFFECT_NAMES[e], n, sp, lo, hi);
        if (e != FX_SOLID) CHECK(changes > 10, "%s n=%d speed=%d looks frozen (%d changes)", EFFECT_NAMES[e], n, sp, changes);
      }
}

static void testPowerLimiter() {
  printf("[power] full-white 24-module chain stays within %d mA budget\n", POWER_BUDGET_MA);
  Renderer r;
  P9813Chain ch;
  ch.begin(PIN_GROVE_CLK, PIN_GROVE_DATA, 3, 6);
  sim.setChain(MAX_CHAIN);
  r.begin(&ch, [](uint8_t, uint8_t, uint8_t) {});
  ShowState s;
  s.on = true; s.level = 254; s.color = {255, 255, 255}; s.effect = FX_SOLID; s.output = OUT_BOTH;
  for (int i = 0; i < 150; ++i) r.frame(s, MAX_CHAIN, 20, false);   // past the 1.5 s wake shimmer
  float ma = 0;
  for (int i = 0; i < MAX_CHAIN; ++i) ma += r.lastChainOut(i) * (MA_PER_CHAIN_CHANNEL / 255.0f);
  printf("         scale=%.3f estimated chain draw=%.0f mA\n", r.lastPowerScale(), ma);
  CHECK(r.lastPowerScale() < 1.0f, "limiter did not engage");
  CHECK(ma <= POWER_BUDGET_MA + 1, "over budget: %.0f mA", ma);
}


static void runLoopMs(uint32_t ms) {
  uint32_t end = sim.nowMs + ms;
  while (sim.nowMs < end) loop();
}

static int litModules() {
  int lit = 0;
  for (auto &c : sim.chips) lit += (c.shown.r + c.shown.g + c.shown.b) > 0;
  return lit;
}

// ------------------------------------------------------------ Demo Reel
static void testDemo() {
  printf("[demo] %d scenes, %.1f s per loop at speed 50, deterministic, cues land\n", DemoReel::segmentCount(),
         DemoReel::loopSeconds());
  RGBf out[MAX_PIXELS], out2[MAX_PIXELS];
  const uint8_t n = 6;

  // Scene order and loop length.
  DemoReel d;
  d.reset(n);
  std::vector<int> order{d.segment()};
  double t = 0, loopEnd = -1;
  bool finite = true;
  float lo = 1e9f, hi = -1e9f;
  float lightningPeak = 0, pumpkinGreen = 0;
  while (t < DemoReel::loopSeconds() + 10) {
    d.render(out, n, 20, 50);
    t += 0.020;
    for (int i = 0; i < n; ++i)
      for (float v : {out[i].r, out[i].g, out[i].b}) {
        if (!std::isfinite(v)) finite = false;
        lo = fminf(lo, v); hi = fmaxf(hi, v);
      }
    const auto &seg = DemoReel::segmentAt(d.segment());
    if (seg.effect == FX_LIGHTNING && seg.kind == DemoReel::SEG_EFFECT && d.segmentTime() > 1.6f && d.segmentTime() < 2.4f)
      for (int i = 0; i < n; ++i) lightningPeak = fmaxf(lightningPeak, out[i].b);
    if (seg.effect == FX_POSSESSED_PUMPKIN && seg.kind == DemoReel::SEG_EFFECT && d.segmentTime() > 5.0f && d.segmentTime() < 6.0f)
      for (int i = 0; i < n; ++i) pumpkinGreen = fmaxf(pumpkinGreen, out[i].g - out[i].r);
    if (d.segment() != order.back()) {
      order.push_back(d.segment());
      if (d.segment() == 0 && loopEnd < 0) loopEnd = t;
    }
  }
  bool inOrder = (int)order.size() >= DemoReel::segmentCount() + 1;
  for (int i = 0; inOrder && i <= DemoReel::segmentCount(); ++i) inOrder = order[i] == i % DemoReel::segmentCount();
  CHECK(inOrder, "scenes out of order (%zu transitions)", order.size());
  // The very first scene starts without a crossfade-in, so lap one is one
  // crossfade longer than the steady loop.
  CHECK(fabs(loopEnd - (DemoReel::loopSeconds() + DemoReel::CROSSFADE_S)) < 0.1, "first lap took %.2f s, expected %.2f",
        loopEnd, DemoReel::loopSeconds() + DemoReel::CROSSFADE_S);
  CHECK(finite && lo >= -1e-4f && hi <= 1.5f, "demo output out of range [%f, %f]", lo, hi);
  CHECK(lightningPeak > 0.6f, "no lightning strike after the 1.6 s cue (peak %.2f)", lightningPeak);
  CHECK(pumpkinGreen > 0.2f, "pumpkin not possessed after the 5 s cue (%.2f)", pumpkinGreen);

  // Every take is the same take.
  DemoReel a, b;
  a.reset(n);
  b.reset(n);
  int diff = 0;
  for (int f = 0; f < 5000; ++f) {
    a.render(out, n, 20, 50);
    b.render(out2, n, 20, 50);
    for (int i = 0; i < n; ++i) diff += memcmp(&out[i], &out2[i], sizeof(RGBf)) != 0;
  }
  CHECK(diff == 0, "two runs of the demo differ in %d pixels", diff);

  // Speed stretches the whole reel.
  DemoReel fast;
  fast.reset(n);
  double tf = 0;
  int segs = 0, last = 0;
  while (segs < DemoReel::segmentCount() && tf < 200) {
    fast.render(out, n, 20, 100);
    tf += 0.020;
    if (fast.segment() != last) { last = fast.segment(); ++segs; }
  }
  const double wantFast = (DemoReel::loopSeconds() + DemoReel::CROSSFADE_S) / 4;
  CHECK(fabs(tf - wantFast) < 0.2, "speed 100 first lap %.2f s, expected ~%.2f", tf, wantFast);

  // Tiny and empty layouts don't break it; a hot-plug mid-demo just joins in.
  DemoReel e;
  e.reset(0);
  for (int f = 0; f < 3000; ++f) e.render(out, f < 1000 ? 0 : f < 2000 ? 1 : MAX_PIXELS, 20, 50);
  CHECK(true, "%s", "survived 0 → 1 → 25 pixels mid-demo");

  // Through the renderer: the scene log fires on start and on each change.
  Renderer r;
  P9813Chain ch;
  ch.begin(PIN_GROVE_CLK, PIN_GROVE_DATA, 3, 6);
  r.begin(&ch, [](uint8_t, uint8_t, uint8_t) {});
  ShowState s;
  s.effect = FX_DEMO_REEL;
  r.frame(s, 3, 20, false);
  CHECK(r.takeDemoSceneChanged() && r.demo().segment() == 0, "demo start not announced");
  int changes = 0;
  for (int f = 0; f < 50 * 20; ++f) { r.frame(s, 3, 20, false); changes += r.takeDemoSceneChanged(); }
  CHECK(changes >= 2, "expected scene changes in 20 s, saw %d", changes);
  s.effect = FX_CANDLE;
  r.frame(s, 3, 20, false);
  s.effect = FX_DEMO_REEL;
  r.frame(s, 3, 20, false);
  CHECK(r.demo().segment() == 0, "re-selecting the demo should restart it from the top");
}

// ----------------------------------------------------------- Matter codes
static int haBrightnessFromLevel(int level) { return (int)lround((level - 1) * 255.0 / 253.0); }   // HA: renormalize((1,254)->(0,255))
static int haLevelFromBrightness(int b) { return (int)lround(b * 253.0 / 255.0 + 1); }              // HA: renormalize((0,255)->(1,254))

static void testMatterCodes() {
  printf("[codes] setup codes, value bands through HA brightness, QR printing\n");
  for (uint32_t bad : {0u, 11111111u, 22222222u, 33333333u, 44444444u, 55555555u, 66666666u, 77777777u, 88888888u,
                       99999999u, 12345678u, 87654321u, 100000000u})
    CHECK(!mcodes::passcodeValid(bad), "%u should be invalid", bad);
  for (uint32_t good : {1u, 20202021u, 99999998u, 12345679u}) CHECK(mcodes::passcodeValid(good), "%u should be valid", good);
  int badGen = 0;
  for (int i = 0; i < 20000; ++i) {
    uint32_t p; uint16_t dsc;
    mcodes::generateSetup(esp_random, p, dsc);
    badGen += !mcodes::passcodeValid(p) || dsc > 0xFFF;
  }
  CHECK(badGen == 0, "%d generated setup codes invalid", badGen);

  int bandErr = 0;
  for (int v = 0; v <= mcodes::BAND_MAX_VALUE; ++v) {
    int L = mcodes::bandToLevel(v);
    bandErr += mcodes::levelToBand(L) != v;
    for (int j = -2; j <= 2; ++j) bandErr += mcodes::levelToBand(L + j) != v;               // ±2 levels of slop
    bandErr += mcodes::levelToBand(haLevelFromBrightness(haBrightnessFromLevel(L))) != v;   // through HA
  }
  CHECK(bandErr == 0, "%d band decoding errors", bandErr);
  int speedErr = 0;
  for (int sp = 1; sp <= 100; ++sp) {
    speedErr += mcodes::levelToSpeed(mcodes::speedToLevel(sp)) != sp;
    int viaPct = mcodes::levelToSpeed(haLevelFromBrightness((int)lround(sp * 255 / 100.0)));     // HA brightness_pct
    speedErr += abs(viaPct - sp) > 1;
  }
  CHECK(speedErr == 0, "%d speed round-trip errors", speedErr);
  int fineErr = 0, levelErr = 0;
  for (int v = 0; v <= mcodes::FINE_MAX_VALUE; ++v) {
    int L = mcodes::fineToLevel(v);
    fineErr += mcodes::levelToFine(L) != v;
    fineErr += mcodes::levelToFine(L - 1) != v;                                              // ±1 level of slop
    fineErr += mcodes::levelToFine(haLevelFromBrightness(haBrightnessFromLevel(L))) != v;   // through HA
  }
  for (int L = 1; L <= 254; ++L) levelErr += haLevelFromBrightness(haBrightnessFromLevel(L)) != L;
  CHECK(fineErr == 0 && levelErr == 0, "fine band: %d errors, HA level round trip: %d errors", fineErr, levelErr);

  char buf[64];
  CHECK(mcodes::payloadFromUrl("https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3AY.K9042C00KA0648G00", buf,
                               sizeof(buf)) && strcmp(buf, "MT:Y.K9042C00KA0648G00") == 0, "payload from URL: %s", buf);
  CHECK(!mcodes::payloadFromUrl("https://example.com/", buf, sizeof(buf)), "URL without payload accepted");
  CHECK(!mcodes::payloadFromUrl("x?data=MT%3AY.K9042C00KA0648G00", buf, 8), "overflow not detected");
  mcodes::formatManualCode("34970112332", buf, sizeof(buf));
  CHECK(strcmp(buf, "3497-011-2332") == 0, "manual code format: %s", buf);

  static std::vector<std::string> lines;
  lines.clear();
  bool ok = mcodes::printQr("MT:Y.K9042C00KA0648G00", true, [](const char *l) { lines.push_back(l); });
  // Version 2 = 25 modules + 2×2 quiet → 29 module rows → 15 text lines, 29 glyphs wide.
  size_t glyphs = 0;
  for (size_t i = 0; i < lines[0].size(); ++i) glyphs += (lines[0][i] & 0xC0) != 0x80;
  CHECK(ok && lines.size() == 15 && glyphs == 29, "QR text %zu lines × %zu glyphs", lines.size(), glyphs);
}

// ------------------------------------------------------------- end-to-end
#if SPOOKY_ZIGBEE
static ZigbeeEP *zbEp(uint8_t n) {
  for (auto *e : Zigbee.eps) if (e->endpoint == n) return e;
  return nullptr;
}
static ZigbeeAnalog *epByNum(uint8_t n) { return static_cast<ZigbeeAnalog *>(zbEp(n)); }

static void testEndToEnd(const char *) {
  printf("[e2e/zigbee] boot with 3 modules, then hot-plug 3 → 5 → 2 while the show runs\n");
  Serial.verbose = true;
  sim.nowMs = 1000;
  sim.chips.clear();
  sim.setChain(3);
  setup();
  CHECK(Zigbee.role == ZIGBEE_ROUTER, "should start as router");
  CHECK(Zigbee.eps.size() == 6, "expected 6 endpoints, got %zu", Zigbee.eps.size());
  CHECK(g_detected == 3, "boot scan saw %u", g_detected);
  for (auto *e : Zigbee.eps) CHECK(e->model && strcmp(e->model, ZB_MODEL) == 0, "ep %u missing model", e->endpoint);
  CHECK(strcmp(ZB_MODEL, BOARD_NAME "-Spooky") == 0, "model %s", ZB_MODEL);

  Zigbee.isConnected = true;          // network joined
  runLoopMs(500);
  auto *count = epByNum(EP_COUNT);
  CHECK(!count->reportedIn.empty() && count->reportedIn.back() == 3, "HA should hear 3");

  sim.setChain(5);                    // plug two more in at the end
  runLoopMs(4000);
  CHECK(count->reportedIn.back() == 5, "after plug-in HA hears %.0f", count->reportedIn.back());
  CHECK(litModules() == 5, "all 5 modules should be lit, %d are", litModules());

  sim.setChain(2);                    // a trick-or-treater kicks the cable
  runLoopMs(4000);
  CHECK(count->reportedIn.back() == 2, "after removal HA hears %.0f", count->reportedIn.back());

  printf("[e2e/zigbee] Home Assistant commands: effect, demo, manual count, spotlight, off\n");
  epByNum(EP_EFFECT)->fromCoordinator(6.0f);
  CHECK(g_show.get().effect == FX_HAUNTED_CHASE, "effect not applied");
  epByNum(EP_EFFECT)->fromCoordinator(99.0f);
  CHECK(g_show.get().effect == FX_COUNT - 1, "effect not clamped");
  epByNum(EP_EFFECT)->fromCoordinator((float)FX_DEMO_REEL);
  std::string log;
  Serial.log = &log;
  runLoopMs(8000);
  Serial.log = nullptr;
  CHECK(log.find("[demo] 1/12 Roll call") != std::string::npos && log.find("[demo] 2/12 Colour wheel") != std::string::npos,
        "demo scenes not announced");
  epByNum(EP_COUNT)->fromCoordinator(4.0f);
  CHECK(effectiveChainCount(g_show.get()) == 4, "manual count ignored");
  epByNum(EP_COUNT)->fromCoordinator(0.0f);
  CHECK(effectiveChainCount(g_show.get()) == 2, "auto count not restored");
  epByNum(EP_SPOTLIGHT)->fromCoordinator(2.0f);
  CHECK(g_show.get().spotlight == 2, "spotlight not applied");

  auto *zbLightEp = static_cast<ZigbeeColorDimmableLight *>(zbEp(EP_LIGHT));
  zbLightEp->fromCoordinator(false, 200, 255, 0, 0);
  runLoopMs(FADE_MS + 200);
  CHECK(litModules() == 0, "light off but %d modules still lit", litModules());

  printf("[e2e/zigbee] button: click cycles effect and reports it to HA\n");
  uint8_t before = g_show.get().effect;
  size_t reports = epByNum(EP_EFFECT)->reportedOut.size();
  sim.buttonLevel = LOW; runLoopMs(200); sim.buttonLevel = HIGH; runLoopMs(50);
  CHECK(g_show.get().effect == (before + 1) % FX_COUNT, "click did not advance effect");
  CHECK(g_show.get().on, "click should turn the light on");
  CHECK(epByNum(EP_EFFECT)->reportedOut.size() == reports + 1, "effect change not reported (%zu→%zu)", reports,
        epByNum(EP_EFFECT)->reportedOut.size());
  CHECK(zbLightEp->state, "light-on from the button not published");

  printf("[e2e/zigbee] settings persist to NVS after the quiet period\n");
  runLoopMs(SETTINGS_SAVE_DELAY_MS + 100);
  CHECK(Preferences::kv()["fx"] == g_show.get().effect, "NVS effect not saved");

  printf("[e2e/zigbee] hold 5 s → factory reset\n");
  sim.buttonLevel = LOW; runLoopMs(BUTTON_FACTORY_RESET_MS + 200); sim.buttonLevel = HIGH; runLoopMs(50);
  CHECK(Zigbee.resets == 1, "factory reset not called");
}
#else
static MatterEndPoint *ep(int id) { return ArduinoMatter::st().endpoints[id - 1]; }
static MatterDimmablePlugin *dim(int id) { return static_cast<MatterDimmablePlugin *>(ep(id)); }
enum { MEP_LIGHT = 1, MEP_EFFECT, MEP_SPEED, MEP_SPOT, MEP_SETUP, MEP_COUNT };

// What the HA integration does: brightness for an exact level (HA's level ↔
// brightness conversion round-trips every level, see testMatterCodes).
static void haSetLevel(int id, int level) {
  dim(id)->fromController(true, (uint8_t)haLevelFromBrightness(haBrightnessFromLevel(level)));
}
static void haSetBand(int id, int value) {
  if (value == 0) { dim(id)->fromController(false, dim(id)->getLevel()); return; }
  haSetLevel(id, mcodes::bandToLevel(value));
}
static int effectShown() { return dim(MEP_EFFECT)->getOnOff() ? mcodes::levelToBand(dim(MEP_EFFECT)->getLevel()) : 0; }

static void testEndToEnd(const char *qrOut) {
  printf("[e2e/matter] boot with 3 modules: endpoint budget, setup code, QR on the serial port\n");
  Serial.verbose = false;
  std::string log;
  Serial.log = &log;
  sim.nowMs = 1000;
  sim.chips.clear();
  sim.setChain(3);
  setup();
  runLoopMs(100);
  auto &M = ArduinoMatter::st();
  CHECK(M.failedEndpoints == 0, "%d endpoints over the %d-endpoint limit of this chip", M.failedEndpoints,
        CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT);
  CHECK(M.endpoints.size() + 1 <= 7, "%zu endpoints + root won't fit an ESP32-H2 (max 7)", M.endpoints.size());
  CHECK(M.endpoints.size() == (size_t)MEP_COUNT, "expected %d endpoints, got %zu", MEP_COUNT, M.endpoints.size());
  CHECK(log.find("ERROR") == std::string::npos, "firmware reported an error at start-up");
  CHECK(log.find("free heap") != std::string::npos, "free heap not logged");
  const char *kinds[] = {"", "color_light", "dimmable_plugin", "dimmable_plugin", "dimmable_plugin", "dimmable_plugin",
                         "light_sensor"};
  for (int id = 1; id <= MEP_COUNT; ++id) CHECK(strcmp(ep(id)->kind, kinds[id]) == 0, "ep %d is %s", id, ep(id)->kind);
  const matterNetwork_t wantNet = MATTER_USE_WIFI ? MATTER_NETWORK_WIFI : MATTER_NETWORK_THREAD;
  CHECK(M.network == wantNet, "network should be %d, is %d", (int)wantNet, (int)M.network);
  CHECK(M.eventCb != nullptr, "no Matter event handler registered");
  CHECK(log.find(MATTER_USE_WIFI ? "Matter over Wi-Fi" : "Matter over Thread") != std::string::npos, "banner doesn't name the network");
  CHECK(M.started && M.vendor == "SpookyLab" && M.product == "Spooky Lights " BOARD_NAME, "identity %s / %s", M.vendor.c_str(),
        M.product.c_str());
  CHECK(mcodes::passcodeValid(M.passcode) && M.passcode != 20202021u && M.discriminator <= 0xFFF, "setup code %u / %u",
        M.passcode, M.discriminator);
  CHECK(Preferences::big()["pin"] == M.passcode && Preferences::big()["disc"] == M.discriminator, "setup code not persisted");
  CHECK(effectShown() == g_show.get().effect, "initial effect %d published as %d", g_show.get().effect, effectShown());
  auto *count = static_cast<MatterLightSensor *>(ep(MEP_COUNT));
  CHECK(count->haLux() == 4.0, "HA should show 3 LEDs as 4.0 lx, shows %.1f", count->haLux());

  CHECK(log.find("Manual pairing code") != std::string::npos && log.find("\xE2\x96\x88") != std::string::npos,
        "pairing banner / QR not printed");
  if (qrOut) {
    std::ofstream f(qrOut);
    f << log << "\n[test] expect passcode=" << M.passcode << " discriminator=" << M.discriminator << "\n";
  }
  size_t firstBanner = log.find("ready to pair");
  runLoopMs(MATTER_QR_REPEAT_MS + 100);
  CHECK(log.find("ready to pair", firstBanner + 1) != std::string::npos, "pairing info not repeated while uncommissioned");

  printf("[e2e/matter] attempt 1: BLE ok, network never joined → no QR mid-pairing, fail-safe explained\n");
  log.clear();
  ArduinoMatter::fire(MATTER_CHIPOBLE_CONNECTION_ESTABLISHED);
  ArduinoMatter::fire(MATTER_COMMISSIONING_SESSION_STARTED);
  runLoopMs(20000);
  ArduinoMatter::fire(MATTER_CHIPOBLE_CONNECTION_CLOSED);   // phone hands over and drops BLE
  runLoopMs(MATTER_QR_REPEAT_MS + 10000);
  CHECK(log.find("connected over Bluetooth") != std::string::npos && log.find("Bluetooth closed") != std::string::npos,
        "BLE milestones not logged");
  CHECK(log.find("ready to pair") == std::string::npos, "QR re-printed while a phone was pairing");
  ArduinoMatter::fire(MATTER_FAIL_SAFE_TIMER_EXPIRED);
  runLoopMs(4000);
  CHECK(log.find("fail-safe expired") != std::string::npos && log.find("never joined") != std::string::npos,
        "failed attempt not explained");
  CHECK(log.find(MATTER_USE_WIFI ? "2.4 GHz" : "Sync Thread credentials") != std::string::npos, "network-specific advice missing");
  CHECK(log.find("ready to pair", log.find("fail-safe expired")) != std::string::npos, "QR not shown again after the failed attempt");
  CHECK(!M.commissioned && ESP.restarts == 0, "a failed attempt must not restart or commission");

  printf("[e2e/matter] attempt 2: BLE → joins network → commissioned\n");
  log.clear();
  ArduinoMatter::fire(MATTER_CHIPOBLE_CONNECTION_ESTABLISHED);
  ArduinoMatter::fire(MATTER_COMMISSIONING_SESSION_STARTED);
  runLoopMs(3000);
  M.netUp = true;
  ArduinoMatter::fire(MATTER_CHIPOBLE_CONNECTION_CLOSED);
  runLoopMs(3000);
  CHECK(log.find(MATTER_USE_WIFI ? "joined the Wi-Fi network" : "joined the Thread network") != std::string::npos,
        "network join not logged");
  ArduinoMatter::fire(MATTER_FABRIC_COMMITTED);
  ArduinoMatter::fire(MATTER_COMMISSIONING_COMPLETE);

  printf("[e2e/matter] commissioned; hot-plug 3 → 5; HA picks effects on the effect dimmer\n");
  M.commissioned = M.online = true;
  log.clear();
  runLoopMs(500);
  CHECK(log.find("commissioned") != std::string::npos && log.find("ready to pair") == std::string::npos, "commissioning not noticed");
  CHECK(radio().connected(), "should report connected");
  sim.setChain(5);
  runLoopMs(4000);
  CHECK(count->haLux() == 6.0, "after plug-in HA should show 6.0 lx, shows %.1f", count->haLux());
  CHECK(litModules() == 5, "all 5 modules should be lit, %d are", litModules());

  int effectErr = 0;
  for (int fx = 0; fx < FX_COUNT; ++fx) {
    haSetBand(MEP_EFFECT, fx);
    runLoopMs(40);
    effectErr += g_show.get().effect != fx || effectShown() != fx;
  }
  CHECK(effectErr == 0, "%d effects did not round-trip through the effect dimmer", effectErr);
  dim(MEP_EFFECT)->fromController(true, (uint8_t)(mcodes::bandToLevel(FX_LIGHTNING) + 3));   // a sloppy controller
  runLoopMs(40);
  CHECK(g_show.get().effect == FX_LIGHTNING && dim(MEP_EFFECT)->getLevel() == mcodes::bandToLevel(FX_LIGHTNING),
        "off-band level should decode and snap back to the band");
  dim(MEP_EFFECT)->fromController(false, dim(MEP_EFFECT)->getLevel());
  runLoopMs(40);
  CHECK(g_show.get().effect == FX_SOLID, "effect dimmer off should mean Solid");

  haSetBand(MEP_EFFECT, FX_DEMO_REEL);
  log.clear();
  runLoopMs(8000);
  CHECK(log.find("[demo] 1/12 Roll call") != std::string::npos && log.find("[demo] 2/12 Colour wheel") != std::string::npos,
        "demo scenes not announced");

  printf("[e2e/matter] speed / spotlight / setup (output + manual count) endpoints\n");
  dim(MEP_SPEED)->fromController(true, (uint8_t)haLevelFromBrightness(204));   // HA "80 %"
  runLoopMs(60);
  CHECK(g_show.get().speed == 80, "speed %u, expected 80", g_show.get().speed);
  CHECK(dim(MEP_SPEED)->getLevel() == mcodes::speedToLevel(80), "speed level not normalised");
  haSetBand(MEP_SPOT, 2);
  int setupErr = 0;
  for (int out = 0; out < OUT_COUNT; ++out)
    for (int man = 0; man <= MAX_CHAIN; ++man) {
      haSetLevel(MEP_SETUP, mcodes::fineToLevel(mcodes::packSetup(out, man)));
      ShowState st = g_show.get();
      setupErr += st.output != out || st.manualCount != man;
    }
  CHECK(setupErr == 0, "%d output/manual combinations did not decode", setupErr);
  haSetLevel(MEP_SETUP, mcodes::fineToLevel(mcodes::packSetup(OUT_GROVE, 4)));
  runLoopMs(60);
  ShowState s = g_show.get();
  CHECK(s.spotlight == 2 && s.manualCount == 4 && s.output == OUT_GROVE, "decoded spot %u manual %u output %u", s.spotlight,
        s.manualCount, s.output);
  CHECK(effectiveChainCount(s) == 4, "manual count not used");
  haSetLevel(MEP_SETUP, mcodes::fineToLevel(0));
  haSetBand(MEP_SPOT, 0);
  runLoopMs(60);
  CHECK(g_show.get().manualCount == 0 && g_show.get().output == OUT_AUTO && g_show.get().spotlight == 0 && !dim(MEP_SPOT)->getOnOff(),
        "resetting setup / spotlight");

  auto *lightEp = static_cast<MatterColorLight *>(ep(MEP_LIGHT));
  lightEp->fromController(true, espHsvColor_t{0, 254, 200});      // red, level 200
  runLoopMs(60);
  s = g_show.get();
  CHECK(s.on && s.level == 200 && s.color.r > 250 && s.color.g < 8 && s.color.b < 8, "colour → (%u,%u,%u) level %u", s.color.r,
        s.color.g, s.color.b, s.level);
  lightEp->fromController(false, lightEp->getColorHSV());
  runLoopMs(FADE_MS + 200);
  CHECK(litModules() == 0, "light off but %d modules still lit", litModules());

  printf("[e2e/matter] button: click → next effect, published on the effect dimmer; long press → on/off\n");
  uint8_t before = g_show.get().effect;
  sim.buttonLevel = LOW; runLoopMs(200); sim.buttonLevel = HIGH; runLoopMs(60);
  uint8_t now = g_show.get().effect;
  CHECK(now == (before + 1) % FX_COUNT && effectShown() == now, "button effect not published (%u vs %d)", now, effectShown());
  CHECK(lightEp->getOnOff(), "light-on from the button not published");
  sim.buttonLevel = LOW; runLoopMs(1500); sim.buttonLevel = HIGH; runLoopMs(60);
  CHECK(!g_show.get().on && !lightEp->getOnOff(), "long press should switch off and publish");

  printf("[e2e/matter] identify, persistence, decommission\n");
  lightEp->identifyFromController(true);
  CHECK((int32_t)(g_identifyUntil - millis()) > 0, "identify ignored");
  lightEp->identifyFromController(false);
  runLoopMs(SETTINGS_SAVE_DELAY_MS + 100);
  CHECK(Preferences::kv()["fx"] == g_show.get().effect && Preferences::kv()["spd"] == 80, "settings not saved");
  sim.buttonLevel = LOW; runLoopMs(BUTTON_FACTORY_RESET_MS + 200); sim.buttonLevel = HIGH;
  runLoopMs(60);
  CHECK(M.decommissions == 1 && ESP.restarts == 1, "long hold should decommission (%d) and restart (%d)", M.decommissions,
        ESP.restarts);
  Serial.log = nullptr;
}
#endif

// --------------------------------------------------------- auto orientation
static void testAutoOrient() {
  printf("[orient] Grove pair wired the other way round is found and remembered\n");
  Serial.verbose = false;
  sim.nowMs = 1000;
  sim.chips.clear();
  sim.wireSwapped(true);
  sim.setChain(4);
  setup();
  CHECK(g_detected == 4 && g_swapped, "swapped wiring: detected %u, swapped %d", g_detected, g_swapped);
  CHECK(Preferences::kv()["swap"] == 1, "orientation not saved");
  runLoopMs(2000);
  CHECK(litModules() == 4, "only %d of 4 modules lit with swapped wiring", litModules());
  sim.setChain(6);
  runLoopMs(4000);
  CHECK(g_detected == 6 && litModules() == 6, "hot-plug with swapped wiring: %u detected, %d lit", g_detected, litModules());
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "all";
  Serial.verbose = false;
  printf("== variant: %s · %s ==\n", BOARD_NAME, SPOOKY_MATTER ? "matter" : "zigbee");
  if (strcmp(mode, "orient") == 0) {
    testAutoOrient();
  } else {
    testEncode();
    testProbeCounts();
    testProbeTiming();
    testEffects();
    testDemo();
    testMatterCodes();
    testPowerLimiter();
    testEndToEnd(argc > 2 ? argv[2] : nullptr);
  }
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
