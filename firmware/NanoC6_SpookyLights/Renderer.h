// =============================================================================
//  Renderer — turns the show state into photons.
//
//  Pipeline per frame (50 fps):
//    layout → effect → spotlight → brightness × fade → gamma → power limit
//          → onboard WS2812 (RMT)  +  Grove P9813 chain (bit-bang, maybe probe)
// =============================================================================
#pragma once
#include <stdint.h>
#include "Effects.h"
#include "P9813Chain.h"

enum OutputMode : uint8_t {
  OUT_AUTO = 0,     // Grove chain if any module is present, otherwise onboard LED
  OUT_ONBOARD = 1,  // onboard LED only
  OUT_GROVE = 2,    // Grove chain only
  OUT_BOTH = 3,     // onboard LED is pixel #1, Grove modules follow
  OUT_COUNT
};

struct ShowState {
  bool on = true;
  uint8_t level = 200;          // Zigbee level 0..254
  RGB8 color = {255, 90, 0};    // pumpkin orange
  uint8_t effect = FX_CANDLE;
  uint8_t speed = 50;           // 1..100
  uint8_t output = OUT_AUTO;
  uint8_t spotlight = 0;        // 0 = off, else 1-based virtual pixel
  uint8_t manualCount = 0;      // 0 = trust detection
};

class Renderer {
public:
  using OnboardWriter = void (*)(uint8_t r, uint8_t g, uint8_t b);

  void begin(P9813Chain *chain, OnboardWriter onboard);

  // Number of virtual pixels the effect should draw for this layout.
  static uint8_t virtualCount(uint8_t chainCount, uint8_t output);

  // Render & output one frame. When `probe` is true the chain refresh also
  // counts modules; the result (see P9813Chain::probe) is returned, else -2.
  int frame(const ShowState &s, uint8_t chainCount, uint32_t dtMs, bool probe);

  // Visual feedback helpers (identify / factory reset)
  void setOverride(RGB8 c, uint32_t untilMs) { _override = c; _overrideUntil = untilMs; }

  // Exposed for tests
  int lastChainOut(uint8_t i) const { return _chainOut[i].r + _chainOut[i].g + _chainOut[i].b; }
  float lastPowerScale() const { return _powerScale; }

private:
  P9813Chain *_chain = nullptr;
  OnboardWriter _onboard = nullptr;
  Effects _fx;
  float _fade = 0;
  RGBf _canvas[MAX_PIXELS];
  RGB8 _chainOut[MAX_CHAIN];
  RGB8 _onboardOut = {0, 0, 0}, _onboardLast = {1, 2, 3};
  uint8_t _gammaLut[1024];
  float _powerScale = 1.0f;
  RGB8 _override = {0, 0, 0};
  uint32_t _overrideUntil = 0;
  uint32_t _clock = 0;

  RGB8 toOutput(RGBf c, float brightness) const;
};
