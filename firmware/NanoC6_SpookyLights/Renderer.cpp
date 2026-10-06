#include "Renderer.h"
#include <math.h>
#include <string.h>

void Renderer::begin(P9813Chain *chain, OnboardWriter onboard) {
  _chain = chain;
  _onboard = onboard;
  for (int i = 0; i < 1024; ++i) {
    float x = i / 1023.0f;
    _gammaLut[i] = (uint8_t)lroundf(powf(x, GAMMA) * 255.0f);
  }
  _fx.begin(0xC0FFEEu ^ 0x31u /* October 31 */);
  memset(_chainOut, 0, sizeof(_chainOut));
}

uint8_t Renderer::virtualCount(uint8_t chainCount, uint8_t output) {
  if (chainCount > MAX_CHAIN) chainCount = MAX_CHAIN;
  switch (output) {
    case OUT_ONBOARD: return 1;
    case OUT_GROVE:   return chainCount;
    case OUT_BOTH:    return 1 + chainCount;
    case OUT_AUTO:
    default:          return chainCount ? chainCount : 1;
  }
}

RGB8 Renderer::toOutput(RGBf c, float brightness) const {
  auto ch = [&](float v) -> uint8_t {
    v *= brightness;
    if (v <= 0) return 0;
    if (v >= 1) return _gammaLut[1023];
    return _gammaLut[(int)(v * 1023.0f + 0.5f)];
  };
  return {ch(c.r), ch(c.g), ch(c.b)};
}

int Renderer::frame(const ShowState &s, uint8_t chainCount, uint32_t dtMs, bool probe) {
  _clock += dtMs;
  if (chainCount > MAX_CHAIN) chainCount = MAX_CHAIN;
  const uint8_t output = s.output < OUT_COUNT ? s.output : (uint8_t)OUT_AUTO;
  const uint8_t n = virtualCount(chainCount, output);

  // 1) effect
  const RGBf base = {s.color.r / 255.0f, s.color.g / 255.0f, s.color.b / 255.0f};
  _fx.render(s.effect, _canvas, n, dtMs, s.speed, base);

  // 2) spotlight: one chosen pixel holds the HA colour steady (the eye in the
  //    attic window) while the rest of the string keeps haunting.
  if (s.spotlight >= 1 && s.spotlight <= n) _canvas[s.spotlight - 1] = base;

  // 3) on/off fade, brightness (perceptual domain)
  float target = s.on ? 1.0f : 0.0f;
  float step = dtMs / (float)FADE_MS;
  if (_fade < target) _fade = fminf(target, _fade + step);
  else if (_fade > target) _fade = fmaxf(target, _fade - step);
  float brightness = (s.level / 254.0f) * _fade;

  // 4) identify / feedback override
  bool overriding = (int32_t)(_overrideUntil - _clock) > 0;

  // 5) route virtual pixels → physical outputs (+ gamma)
  RGB8 onboard = {0, 0, 0};
  memset(_chainOut, 0, sizeof(_chainOut));
  bool onboardUsed = (output == OUT_ONBOARD || output == OUT_BOTH || (output == OUT_AUTO && chainCount == 0));
  bool chainUsed = (output == OUT_GROVE || output == OUT_BOTH || (output == OUT_AUTO && chainCount > 0));
  uint8_t v = 0;
  if (onboardUsed) onboard = overriding ? _override : toOutput(_canvas[v++], brightness);
  if (chainUsed)
    for (uint8_t i = 0; i < chainCount; ++i)
      _chainOut[i] = overriding ? _override : toOutput(_canvas[v++], brightness);

  // 6) power limiter — scale the whole frame so we stay within budget
  uint32_t sumChain = 0;
  for (uint8_t i = 0; i < chainCount; ++i) sumChain += _chainOut[i].r + _chainOut[i].g + _chainOut[i].b;
  uint32_t sumOnboard = onboard.r + onboard.g + onboard.b;
  float ma = sumChain * (MA_PER_CHAIN_CHANNEL / 255.0f) + sumOnboard * (MA_PER_ONBOARD_CHANNEL / 255.0f);
  _powerScale = (ma > POWER_BUDGET_MA) ? POWER_BUDGET_MA / ma : 1.0f;
  if (_powerScale < 1.0f) {
    auto sc = [&](RGB8 &c) {
      c.r = (uint8_t)(c.r * _powerScale);
      c.g = (uint8_t)(c.g * _powerScale);
      c.b = (uint8_t)(c.b * _powerScale);
    };
    for (uint8_t i = 0; i < chainCount; ++i) sc(_chainOut[i]);
    sc(onboard);
  }

  // 7) out the door
  if (_onboard && (onboard.r != _onboardLast.r || onboard.g != _onboardLast.g || onboard.b != _onboardLast.b)) {
    _onboard(onboard.r, onboard.g, onboard.b);
    _onboardLast = onboard;
  }
  int result = -2;
  if (_chain) {
    if (probe) {
      // Full-length refresh: also blanks any modules beyond `chainCount`
      // (e.g. a manual count lower than what is physically plugged in).
#if CHAIN_DETECT_LOOPBACK
      result = _chain->probe(_chainOut, MAX_CHAIN, MAX_CHAIN + 1);
#else
      _chain->write(_chainOut, MAX_CHAIN);
#endif
    } else {
      _chain->write(_chainOut, chainCount);
    }
  }
  return result;
}
