// =============================================================================
//  Effects — the Halloween show. Every effect renders into a "virtual canvas"
//  of n pixels, so the same effect works on the single onboard LED, on a
//  3-module Grove chain, or on 24 modules — and re-flows the instant a module
//  is plugged in or pulled out.
// =============================================================================
#pragma once
#include <stdint.h>
#include "P9813Chain.h"   // RGB8
#include "config.h"

#define MAX_PIXELS (MAX_CHAIN + 1)   // chain + onboard LED in "both" mode

struct RGBf {
  float r, g, b;
};

enum EffectId : uint8_t {
  FX_SOLID = 0,
  FX_CANDLE,
  FX_GHOST_BREATH,
  FX_LIGHTNING,
  FX_CAULDRON,
  FX_HEARTBEAT,
  FX_HAUNTED_CHASE,
  FX_POLTERGEIST,
  FX_POSSESSED_PUMPKIN,
  FX_BLOOD_MOON,
  FX_COUNT
};

// Keep in sync with custom_components/spooky_lights/const.py (EFFECTS).
extern const char *const EFFECT_NAMES[FX_COUNT];

class Effects {
public:
  void begin(uint32_t seed);

  // Called when the pixel count changes: restarts per-pixel state so that a
  // freshly connected module "wakes up" instead of popping in at full blast.
  void relayout(uint8_t n);

  // Render one frame. `dtMs` real milliseconds since last frame, `speed`
  // 1..100 (50 = normal), `base` is the colour chosen in Home Assistant.
  // Output is linear light, 0..1 per channel, before brightness & gamma.
  void render(uint8_t effect, RGBf *out, uint8_t n, uint32_t dtMs, uint8_t speed, RGBf base);

  // 0..1 per-pixel "awakening" envelope applied after relayout (exposed so the
  // renderer can include it, and for tests).
  float wake(uint8_t i) const { return _wake[i]; }

private:
  // time & randomness
  float _t = 0;            // effect time in seconds (speed-scaled)
  uint32_t _rng = 0x9E3779B9u;
  uint32_t rnd();
  float rndf();                       // 0..1
  float rndr(float a, float b) { return a + (b - a) * rndf(); }

  uint8_t _n = 0;
  float _wake[MAX_PIXELS];

  // generic per-pixel state, re-interpreted by each effect
  float _a[MAX_PIXELS], _b[MAX_PIXELS], _c[MAX_PIXELS];
  RGBf _col[MAX_PIXELS];

  // scalar state
  float _timer = 0, _timer2 = 0, _pos = 0, _dir = 1;
  int _phase = 0, _flashes = 0, _center = 0;
  uint8_t _lastEffect = 255;

  void resetState();

  void fxSolid(RGBf *o, uint8_t n, RGBf base);
  void fxCandle(RGBf *o, uint8_t n, float dt, RGBf base);
  void fxGhost(RGBf *o, uint8_t n, RGBf base);
  void fxLightning(RGBf *o, uint8_t n, float dt, RGBf base);
  void fxCauldron(RGBf *o, uint8_t n, float dt);
  void fxHeartbeat(RGBf *o, uint8_t n, RGBf base);
  void fxChase(RGBf *o, uint8_t n, float dt, RGBf base);
  void fxPoltergeist(RGBf *o, uint8_t n, float dt, RGBf base);
  void fxPumpkin(RGBf *o, uint8_t n, float dt);
  void fxBloodMoon(RGBf *o, uint8_t n);
};

// small colour helpers shared with the renderer
RGBf hsv2rgb(float h, float s, float v);
RGBf mix(RGBf a, RGBf b, float t);
RGBf scale(RGBf a, float k);
