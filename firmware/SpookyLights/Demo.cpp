#include "Demo.h"
#include <math.h>

// The script. Times are seconds at speed 50. Cues land after the crossfade-in
// (≥ CROSSFADE_S) so each signature move happens with the segment fully on air.
static const DemoReel::Segment SCRIPT[] = {
  {DemoReel::SEG_ROLL_CALL,    0,                    {0, 0, 0},       6.0f, -1.0f, -1.0f, "Roll call"},
  {DemoReel::SEG_COLOUR_WHEEL, 0,                    {0, 0, 0},       8.0f, -1.0f, -1.0f, "Colour wheel"},
  {DemoReel::SEG_EFFECT,       FX_CANDLE,            {255, 90, 0},    8.0f, 5.0f,  -1.0f, "Candle Flicker"},
  {DemoReel::SEG_EFFECT,       FX_GHOST_BREATH,      {120, 170, 255}, 8.0f, -1.0f, -1.0f, "Ghost Breath"},
  {DemoReel::SEG_EFFECT,       FX_LIGHTNING,         {150, 170, 255}, 10.0f, 1.6f, 6.0f,  "Lightning Storm"},
  {DemoReel::SEG_EFFECT,       FX_CAULDRON,          {0, 200, 40},    8.0f, 4.0f,  -1.0f, "Witch's Cauldron"},
  {DemoReel::SEG_EFFECT,       FX_HEARTBEAT,         {255, 0, 20},    8.0f, -1.0f, -1.0f, "Tell-Tale Heart"},
  {DemoReel::SEG_EFFECT,       FX_HAUNTED_CHASE,     {80, 255, 40},   8.0f, 4.5f,  -1.0f, "Haunted Chase"},
  {DemoReel::SEG_EFFECT,       FX_POLTERGEIST,       {150, 0, 255},   8.0f, 3.0f,  6.0f,  "Poltergeist"},
  {DemoReel::SEG_EFFECT,       FX_POSSESSED_PUMPKIN, {255, 90, 0},    12.0f, 5.0f, -1.0f, "Possessed Pumpkin"},
  {DemoReel::SEG_EFFECT,       FX_BLOOD_MOON,        {180, 0, 0},     8.0f, -1.0f, -1.0f, "Blood Moon"},
  {DemoReel::SEG_FINALE,       0,                    {255, 140, 40},  4.0f, -1.0f, -1.0f, "Finale"},
};
static constexpr int SEGMENTS = sizeof(SCRIPT) / sizeof(SCRIPT[0]);

int DemoReel::segmentCount() { return SEGMENTS; }
const DemoReel::Segment &DemoReel::segmentAt(int i) { return SCRIPT[i]; }

float DemoReel::loopSeconds() {
  float s = 0;
  for (int i = 0; i < SEGMENTS; ++i) s += SCRIPT[i].seconds - CROSSFADE_S;
  return s;
}

static inline float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline float smooth(float x) {
  x = clamp01(x);
  return x * x * (3.0f - 2.0f * x);
}

void DemoReel::reset(uint8_t n) {
  _n = n > MAX_PIXELS ? MAX_PIXELS : n;
  _seg = 0;
  _t = 0;
  _active = 0;
  _nextStarted = false;
  _cued1 = _cued2 = false;
  _changed = true;
  startSegment(_fx[0], 0, _n);
}

void DemoReel::startSegment(Effects &fx, int seg, uint8_t n) {
  // A fixed seed per segment makes every loop — and every take — identical.
  fx.begin(0xD3E0u + (uint32_t)seg * 7919u);
  fx.prime(n);
}

void DemoReel::renderSegment(int seg, Effects &fx, float t, RGBf *out, uint8_t n, uint32_t dtMs,
                             uint8_t speed) {
  const Segment &s = SCRIPT[seg];
  switch (s.kind) {
    case SEG_ROLL_CALL: {
      // 0–3 s: pixels answer one by one, each in its own hue (shows every
      // module is individually addressable, and how many were detected).
      // 3.0–3.6 s: a white "count off" flash. Then the rainbow slowly turns.
      float lit = (t / 3.0f) * n;
      for (uint8_t i = 0; i < n; ++i) {
        float hue = (float)i / (n > 1 ? n : 1) + (t > 3.6f ? (t - 3.6f) * 0.05f : 0.0f);
        float on = clamp01(lit - i);                  // fades in over ~one step
        float pop = (lit - i > 0 && lit - i < 1.0f) ? 0.35f * (1.0f - (lit - i)) : 0.0f;
        RGBf c = scale(hsv2rgb(hue, 1.0f, 1.0f), on);
        c = mix(c, {1, 1, 1}, pop);
        if (t >= 3.0f && t < 3.6f) c = mix(c, {1, 1, 1}, sinf((t - 3.0f) / 0.6f * 3.14159265f));
        out[i] = c;
      }
      break;
    }
    case SEG_COLOUR_WHEEL:
      for (uint8_t i = 0; i < n; ++i)
        out[i] = hsv2rgb((float)i / (n > 1 ? n : 1) * 0.75f + t * 0.12f, 1.0f, 1.0f);
      break;
    case SEG_FINALE: {
      const RGBf ember = {s.color.r / 255.0f, s.color.g / 255.0f, s.color.b / 255.0f};
      RGBf c;
      if (t < 1.2f) c = (fmodf(t, 0.4f) < 0.12f) ? RGBf{1, 1, 1} : RGBf{0, 0, 0};   // three strobes
      else c = scale(ember, 1.0f - smooth((t - 1.2f) / 2.0f));                     // and fade out
      for (uint8_t i = 0; i < n; ++i) out[i] = c;
      break;
    }
    case SEG_EFFECT:
    default: {
      const RGBf base = {s.color.r / 255.0f, s.color.g / 255.0f, s.color.b / 255.0f};
      fx.render(s.effect, out, n, dtMs, speed, base);
      break;
    }
  }
}

void DemoReel::render(RGBf *out, uint8_t n, uint32_t dtMs, uint8_t speed) {
  if (n > MAX_PIXELS) n = MAX_PIXELS;
  _n = n;
  if (speed < 1) speed = 1;
  if (speed > 100) speed = 100;
  float realDt = dtMs * 0.001f;
  if (realDt > 0.1f) realDt = 0.1f;
  const float dt = realDt * powf(2.0f, (speed - 50) / 25.0f);   // same law as Effects

  const Segment &cur = SCRIPT[_seg];
  const int nextSeg = (_seg + 1) % SEGMENTS;
  Effects &fx = _fx[_active];

  if (cur.kind == SEG_EFFECT) {
    if (!_cued1 && cur.cue1 >= 0 && _t >= cur.cue1) { fx.cue(cur.effect); _cued1 = true; }
    if (!_cued2 && cur.cue2 >= 0 && _t >= cur.cue2) { fx.cue(cur.effect); _cued2 = true; }
  }
  renderSegment(_seg, fx, _t, out, n, dtMs, speed);

  const float xfStart = cur.seconds - CROSSFADE_S;
  if (_t >= xfStart) {
    Effects &nx = _fx[_active ^ 1];
    if (!_nextStarted) {
      startSegment(nx, nextSeg, n);
      _nextStarted = true;
    }
    renderSegment(nextSeg, nx, _t - xfStart, _buf, n, dtMs, speed);
    const float k = smooth((_t - xfStart) / CROSSFADE_S);
    for (uint8_t i = 0; i < n; ++i) out[i] = mix(out[i], _buf[i], k);
  }

  _t += dt;
  if (_t >= cur.seconds) {                 // hand over: the next segment is already running
    _t -= xfStart;
    _seg = nextSeg;
    _active ^= 1;
    _nextStarted = false;
    _cued1 = _cued2 = false;
    _changed = true;
  }
}
