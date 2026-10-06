// Host tests for NanoC6 Spooky Lights firmware logic.
// Build & run: tools/hosttest/run.sh
#define SIM_IMPLEMENTATION
#include "sim.h"
#include "Zigbee.h"
ZigbeeCore Zigbee;

// Pull the real sketch into this translation unit (its statics become visible).
#include "../../firmware/NanoC6_SpookyLights/NanoC6_SpookyLights.ino"

#include <random>
#include <cstdlib>

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
  printf("[effects] %d effects × sizes × speeds: finite, bounded, alive\n", FX_COUNT);
  Effects fx;
  fx.begin(1234);
  RGBf out[MAX_PIXELS];
  for (int e = 0; e < FX_COUNT; ++e)
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

static ZigbeeAnalog *epByNum(uint8_t n) {
  for (auto *e : Zigbee.eps) if (e->endpoint == n) return static_cast<ZigbeeAnalog *>(e);
  return nullptr;
}

static void testEndToEnd() {
  printf("[e2e] boot with 3 modules, then hot-plug 3 → 5 → 2 while the show runs\n");
  Serial.verbose = true;
  sim.nowMs = 1000;
  sim.chips.clear();
  sim.setChain(3);
  setup();
  CHECK(Zigbee.role == ZIGBEE_ROUTER, "should start as router");
  CHECK(Zigbee.eps.size() == 6, "expected 6 endpoints, got %zu", Zigbee.eps.size());
  CHECK(g_detected == 3, "boot scan saw %u", g_detected);
  for (auto *e : Zigbee.eps) CHECK(e->model && strcmp(e->model, ZB_MODEL) == 0, "ep %u missing model", e->endpoint);

  Zigbee.isConnected = true;          // network joined
  runLoopMs(500);
  auto *count = epByNum(EP_COUNT);
  CHECK(!count->reportedIn.empty() && count->reportedIn.back() == 3, "HA should hear 3");

  sim.setChain(5);                    // plug two more in at the end
  runLoopMs(4000);
  CHECK(count->reportedIn.back() == 5, "after plug-in HA hears %.0f", count->reportedIn.back());
  int lit = 0;
  for (int i = 0; i < 5; ++i) lit += (sim.chips[i].shown.r + sim.chips[i].shown.g + sim.chips[i].shown.b) > 0;
  CHECK(lit == 5, "all 5 modules should be lit, %d are", lit);

  sim.setChain(2);                    // a trick-or-treater kicks the cable
  runLoopMs(4000);
  CHECK(count->reportedIn.back() == 2, "after removal HA hears %.0f", count->reportedIn.back());

  printf("[e2e] Home Assistant commands: effect, manual count, spotlight, off\n");
  epByNum(EP_EFFECT)->fromCoordinator(6.0f);
  CHECK(snapshot().effect == FX_HAUNTED_CHASE, "effect not applied");
  epByNum(EP_EFFECT)->fromCoordinator(99.0f);
  CHECK(snapshot().effect == FX_COUNT - 1, "effect not clamped");
  epByNum(EP_COUNT)->fromCoordinator(4.0f);
  CHECK(effectiveChainCount(snapshot()) == 4, "manual count ignored");
  epByNum(EP_COUNT)->fromCoordinator(0.0f);
  CHECK(effectiveChainCount(snapshot()) == 2, "auto count not restored");
  epByNum(EP_SPOTLIGHT)->fromCoordinator(2.0f);
  CHECK(snapshot().spotlight == 2, "spotlight not applied");

  zbLight.fromCoordinator(false, 200, 255, 0, 0);
  runLoopMs(FADE_MS + 200);
  int dark = 0;
  for (auto &c : sim.chips) dark += (c.shown.r + c.shown.g + c.shown.b) == 0;
  CHECK(dark == (int)sim.chips.size(), "light off but %zu-%d modules still lit", sim.chips.size(), dark);

  printf("[e2e] button: click cycles effect and reports it to HA\n");
  uint8_t before = snapshot().effect;
  size_t reports = epByNum(EP_EFFECT)->reportedOut.size();
  sim.buttonLevel = LOW; runLoopMs(200); sim.buttonLevel = HIGH; runLoopMs(50);
  CHECK(snapshot().effect == (before + 1) % FX_COUNT, "click did not advance effect");
  CHECK(snapshot().on, "click should turn the light on");
  CHECK(epByNum(EP_EFFECT)->reportedOut.size() == reports + 1, "effect change not reported");

  printf("[e2e] settings persist to NVS after the quiet period\n");
  runLoopMs(SETTINGS_SAVE_DELAY_MS + 100);
  CHECK(Preferences::kv()["fx"] == snapshot().effect, "NVS effect not saved");
}

int main() {
  Serial.verbose = false;
  testEncode();
  testProbeCounts();
  testProbeTiming();
  testEffects();
  testPowerLimiter();
  testEndToEnd();
  printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
