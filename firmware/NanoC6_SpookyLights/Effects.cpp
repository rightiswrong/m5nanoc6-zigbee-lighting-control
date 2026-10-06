#include "Effects.h"
#include <math.h>

const char *const EFFECT_NAMES[FX_COUNT] = {
  "Solid",
  "Candle Flicker",
  "Ghost Breath",
  "Lightning Storm",
  "Witch's Cauldron",
  "Tell-Tale Heart",
  "Haunted Chase",
  "Poltergeist",
  "Possessed Pumpkin",
  "Blood Moon",
};

static const float TAU = 6.2831853f;

// ----------------------------------------------------------------- helpers
RGBf mix(RGBf a, RGBf b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
RGBf scale(RGBf a, float k) { return {a.r * k, a.g * k, a.b * k}; }

static inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float approach(float cur, float target, float rate, float dt) {
  // frame-rate independent exponential approach
  float k = 1.0f - expf(-rate * dt);
  return cur + (target - cur) * k;
}

RGBf hsv2rgb(float h, float s, float v) {
  h = fmodf(h, 1.0f);
  if (h < 0) h += 1.0f;
  float i = floorf(h * 6.0f), f = h * 6.0f - i;
  float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch ((int)i % 6) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
  }
}

uint32_t Effects::rnd() {           // xorshift32 — tiny and plenty random for ghosts
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}
float Effects::rndf() { return (rnd() >> 8) * (1.0f / 16777216.0f); }

// ------------------------------------------------------------ life-cycle
void Effects::begin(uint32_t seed) {
  _rng = seed ? seed : 0x9E3779B9u;
  for (int i = 0; i < MAX_PIXELS; ++i) _wake[i] = 0;
  resetState();
}

void Effects::resetState() {
  for (int i = 0; i < MAX_PIXELS; ++i) {
    _a[i] = 0;
    _b[i] = rndf();
    _c[i] = rndf() * 0.2f;
    _col[i] = {0, 0, 0};
  }
  _timer = rndr(1.0f, 3.0f);
  _timer2 = 0;
  _pos = 0;
  _dir = 1;
  _phase = 0;
  _flashes = 0;
  _center = 0;
}

void Effects::relayout(uint8_t n) {
  if (n > MAX_PIXELS) n = MAX_PIXELS;
  // Newly-arrived pixels start "asleep" and are summoned awake by render().
  for (uint8_t i = _n; i < n; ++i) {
    _wake[i] = 0;
    _a[i] = 0;
    _b[i] = rndf();
    _c[i] = 0;
  }
  if (_pos >= n) _pos = n ? n - 1 : 0;
  _n = n;
}

// ------------------------------------------------------------------ render
void Effects::render(uint8_t effect, RGBf *out, uint8_t n, uint32_t dtMs, uint8_t speed, RGBf base) {
  if (n > MAX_PIXELS) n = MAX_PIXELS;
  if (n != _n) relayout(n);
  if (effect >= FX_COUNT) effect = FX_SOLID;
  if (effect != _lastEffect) {
    resetState();
    _lastEffect = effect;
  }

  float realDt = dtMs * 0.001f;
  if (realDt > 0.1f) realDt = 0.1f;                       // after a stall, don't jump
  if (speed < 1) speed = 1;
  if (speed > 100) speed = 100;
  float factor = powf(2.0f, (speed - 50) / 25.0f);        // 1 → ~0.25x, 50 → 1x, 100 → 4x
  float dt = realDt * factor;
  _t += dt;
  if (_t > 3600.0f) _t -= 3600.0f;                        // keep float precision healthy

  switch (effect) {
    case FX_SOLID:             fxSolid(out, n, base); break;
    case FX_CANDLE:            fxCandle(out, n, dt, base); break;
    case FX_GHOST_BREATH:      fxGhost(out, n, base); break;
    case FX_LIGHTNING:         fxLightning(out, n, dt, base); break;
    case FX_CAULDRON:          fxCauldron(out, n, dt); break;
    case FX_HEARTBEAT:         fxHeartbeat(out, n, base); break;
    case FX_HAUNTED_CHASE:     fxChase(out, n, dt, base); break;
    case FX_POLTERGEIST:       fxPoltergeist(out, n, dt, base); break;
    case FX_POSSESSED_PUMPKIN: fxPumpkin(out, n, dt); break;
    case FX_BLOOD_MOON:        fxBloodMoon(out, n); break;
    default:                   fxSolid(out, n, base); break;
  }

  // Awakening: a newly plugged-in module shivers with pale ectoplasm for
  // ~1.5 s, then settles into whatever the show is doing.
  const RGBf ecto = {0.55f, 0.75f, 1.0f};
  for (uint8_t i = 0; i < n; ++i) {
    if (_wake[i] >= 1.0f) continue;
    float w = _wake[i];
    float shiver = 0.5f + 0.5f * sinf((1.0f - w) * 37.0f + i);   // ~4 Hz shiver
    out[i] = mix(out[i], scale(ecto, 0.65f * shiver), 1.0f - w);
    _wake[i] = clamp01(w + realDt / 1.5f);
  }
}

// ----------------------------------------------------------------- effects
void Effects::fxSolid(RGBf *o, uint8_t n, RGBf base) {
  for (uint8_t i = 0; i < n; ++i) o[i] = base;
}

// Each pixel is its own little flame: a random walk toward a new target every
// 40–160 ms, with the occasional gust that almost blows it out. When a flame
// dims it also reddens (embers are redder than flame tips).
void Effects::fxCandle(RGBf *o, uint8_t n, float dt, RGBf base) {
  for (uint8_t i = 0; i < n; ++i) {
    _c[i] -= dt;
    if (_c[i] <= 0) {
      _b[i] = (rndf() < 0.06f) ? rndr(0.12f, 0.3f) : rndr(0.55f, 1.0f);
      _c[i] = rndr(0.04f, 0.16f);
    }
    _a[i] = approach(_a[i], _b[i], 18.0f, dt);
    float lvl = _a[i];
    RGBf c = scale(base, lvl);
    float ember = 1.0f - lvl;                    // dimmer → redder
    c.g *= 1.0f - 0.55f * ember;
    c.b *= 1.0f - 0.85f * ember;
    o[i] = c;
  }
}

// A slow breath that rolls down the chain, as if something pale were drifting
// past each window in turn. Peaks bleach toward white like a sheet in moonlight.
void Effects::fxGhost(RGBf *o, uint8_t n, RGBf base) {
  const RGBf pale = {0.85f, 0.9f, 1.0f};
  for (uint8_t i = 0; i < n; ++i) {
    float s = 0.5f - 0.5f * cosf(TAU * (_t / 4.0f) - i * 0.55f);
    float lvl = 0.03f + 0.97f * s * s;           // squared = longer dark lull
    o[i] = scale(mix(base, pale, 0.35f * s), lvl);
  }
}

// A brooding dark-blue sky; every few seconds a strike made of 1–4 stutters,
// centred somewhere along the chain and spilling to neighbours.
void Effects::fxLightning(RGBf *o, uint8_t n, float dt, RGBf base) {
  const RGBf sky = mix({0.0f, 0.01f, 0.06f}, scale(base, 0.05f), 0.3f);
  const RGBf bolt = {0.85f, 0.88f, 1.0f};
  _timer -= dt;
  if (_phase == 0 && _timer <= 0) {               // begin a strike
    _phase = 1;
    _flashes = 1 + (rnd() % 4);
    _center = n ? (rnd() % n) : 0;
    _timer = 0;
  }
  if (_phase == 1 && _timer <= 0) {
    if (_flashes > 0) {
      float peak = rndr(0.6f, 1.0f);
      float reach = rndr(1.0f, 4.0f);
      for (uint8_t i = 0; i < n; ++i) {
        float d = fabsf((float)i - _center) / reach;
        float k = peak * expf(-d * d);
        if (k > _a[i]) _a[i] = k;
      }
      --_flashes;
      _timer = rndr(0.05f, 0.16f);                // gap until next stutter
    } else {
      _phase = 0;
      _timer = rndr(3.0f, 9.0f);                  // calm before the next storm
    }
  }
  for (uint8_t i = 0; i < n; ++i) {
    _a[i] *= expf(-dt * 14.0f);                   // fast afterglow decay
    o[i] = mix(sky, bolt, clamp01(_a[i]));
  }
}

// Slowly churning emerald ⇄ violet brew; bubbles rise (brighten) and pop.
void Effects::fxCauldron(RGBf *o, uint8_t n, float dt) {
  const RGBf brew = {0.0f, 0.75f, 0.12f};
  const RGBf hex = {0.45f, 0.0f, 0.85f};
  const RGBf foam = {0.6f, 1.0f, 0.3f};
  for (uint8_t i = 0; i < n; ++i) {
    float swirl = 0.5f + 0.5f * sinf(_t * 0.7f + _b[i] * TAU + i * 0.9f);
    RGBf c = scale(mix(brew, hex, swirl * swirl), 0.35f + 0.25f * swirl);
    _c[i] -= dt;
    if (_c[i] <= 0 && _a[i] < 0.05f && rndf() < dt * 0.8f) {
      _a[i] = 0.01f;                              // a bubble starts to rise
      _c[i] = rndr(0.5f, 2.0f);
    }
    if (_a[i] > 0) {
      _a[i] += dt * 1.6f;                         // swell...
      if (_a[i] >= 1.0f) _a[i] = -1.0f;           // ...and pop
      c = mix(c, foam, _a[i] * _a[i]);
    } else if (_a[i] < 0) {
      _a[i] += dt * 6.0f;                         // pop flash fades fast
      if (_a[i] > 0) _a[i] = 0;
      c = mix(c, {1, 1, 1}, -_a[i] * 0.6f);
    }
    o[i] = c;
  }
}

// lub-dub ... lub-dub. The beat ripples outward from the middle of the chain,
// so a long string looks like it is pumping.
void Effects::fxHeartbeat(RGBf *o, uint8_t n, RGBf base) {
  const float period = 1.1f;
  float mid = (n - 1) * 0.5f;
  for (uint8_t i = 0; i < n; ++i) {
    float delay = fabsf(i - mid) * 0.035f;
    float ph = fmodf(_t - delay + 100.0f * period, period);
    float lub = expf(-ph * 14.0f);
    float dubT = ph - 0.27f;
    float dub = dubT > 0 ? 0.7f * expf(-dubT * 12.0f) : 0.0f;
    float lvl = 0.06f + 0.94f * clamp01(lub + dub);
    o[i] = scale(base, lvl);
  }
}

// One glowing eye with a comet tail prowls the chain, sometimes doubling back.
// On a single LED it becomes an eye that watches... and blinks.
void Effects::fxChase(RGBf *o, uint8_t n, float dt, RGBf base) {
  if (n <= 1) {
    _timer -= dt;
    float lvl = 1.0f;
    if (_timer < 0.18f) lvl = clamp01(fabsf(_timer - 0.09f) / 0.09f);  // the blink
    if (_timer <= 0) _timer = rndr(2.0f, 6.0f);
    if (n) o[0] = scale(base, 0.15f + 0.85f * lvl);
    return;
  }
  _pos += _dir * dt * 5.0f;                       // ~5 pixels per second
  if (_pos >= n - 1) { _pos = n - 1; _dir = -1; }
  if (_pos <= 0) { _pos = 0; _dir = 1; }
  if (rndf() < dt * 0.25f) _dir = -_dir;          // it heard something
  for (uint8_t i = 0; i < n; ++i) {
    _a[i] *= expf(-dt * 4.0f);                    // tail fades
    float d = fabsf(i - _pos);
    if (d < 1.0f && 1.0f - d > _a[i]) _a[i] = 1.0f - d;
    o[i] = scale(base, 0.02f + 0.98f * _a[i] * _a[i]);
  }
}

// Mostly dim... then a random pixel is thrown into a lurid colour and decays,
// and every so often the whole string stutters as if the power was grabbed.
void Effects::fxPoltergeist(RGBf *o, uint8_t n, float dt, RGBf base) {
  if (n && rndf() < dt * 3.0f) {
    uint8_t i = rnd() % n;
    _col[i] = hsv2rgb(rndf(), rndr(0.7f, 1.0f), 1.0f);
    _a[i] = 1.0f;
  }
  _timer -= dt;                                   // stutter scheduler
  if (_timer <= 0) {
    _timer2 = 0.45f;                              // stutter duration
    _timer = rndr(6.0f, 14.0f);
  }
  float stutter = 1.0f;
  if (_timer2 > 0) {
    _timer2 -= dt;
    stutter = (fmodf(_timer2, 0.09f) < 0.045f) ? 0.0f : 1.4f;
  }
  for (uint8_t i = 0; i < n; ++i) {
    _a[i] *= expf(-dt * 2.2f);
    RGBf c = mix(scale(base, 0.08f), _col[i], _a[i]);
    o[i] = scale(c, stutter);
  }
}

// A happy jack-o'-lantern candle... until something else moves in. Every
// 10–25 s the flame turns sickly green and jitters, then returns as if nothing
// happened. Ignores the HA colour on purpose: pumpkins are orange.
void Effects::fxPumpkin(RGBf *o, uint8_t n, float dt) {
  const RGBf orange = {1.0f, 0.38f, 0.02f};
  const RGBf sick = {0.35f, 1.0f, 0.0f};
  fxCandle(o, n, dt, orange);
  _timer -= dt;
  if (_phase == 0 && _timer <= 0) { _phase = 1; _timer2 = rndr(1.2f, 2.2f); }
  if (_phase == 1) {
    _timer2 -= dt;
    float jitter = rndr(0.4f, 1.3f);
    for (uint8_t i = 0; i < n; ++i) o[i] = mix(o[i], scale(sick, jitter), 0.85f);
    if (_timer2 <= 0) { _phase = 0; _timer = rndr(10.0f, 25.0f); }
  }
}

// A slow, heavy tide of deep crimson, rust and near-black.
void Effects::fxBloodMoon(RGBf *o, uint8_t n) {
  const RGBf blood = {0.7f, 0.0f, 0.0f};
  const RGBf rust = {1.0f, 0.22f, 0.0f};
  const RGBf night = {0.08f, 0.0f, 0.01f};
  for (uint8_t i = 0; i < n; ++i) {
    float w = 0.5f + 0.5f * sinf(_t * 0.35f - i * 0.45f);
    float w2 = 0.5f + 0.5f * sinf(_t * 0.13f + i * 0.21f + 1.7f);
    RGBf c = w < 0.5f ? mix(night, blood, w * 2.0f) : mix(blood, rust, (w - 0.5f) * 2.0f * w2);
    o[i] = c;
  }
}
