// =============================================================================
//  DemoReel — a scripted, repeatable tour of everything the light can do,
//  made for filming. ~96 s per loop at speed 50 (the speed control stretches
//  or squeezes the whole reel), and identical on every take: each segment
//  starts from a fixed random seed, so a retake shows the same strikes, the
//  same bubbles and the same possession at the same moment.
//
//    0  Roll call          pixels light one by one, then a white "count off"
//    1  Colour wheel       a rainbow rolling down the string (full RGB range)
//    2…10                  every effect, each cued to show its signature move
//   11  Finale             three strobes, fade to black, loop
//
//  Segments overlap by a 1.2 s crossfade. Scene changes are announced on the
//  serial log ("[demo] 5/12 Lightning Storm") so you can line up captions.
// =============================================================================
#pragma once
#include <stdint.h>
#include "Effects.h"

class DemoReel {
public:
  struct Segment {
    uint8_t kind;          // SEG_* below
    uint8_t effect;        // for SEG_EFFECT
    RGB8 color;            // base colour handed to the effect
    float seconds;         // length at speed 50 (excluding nothing: crossfade is inside)
    float cue1, cue2;      // seconds into the segment to fire Effects::cue(), <0 = none
    const char *title;
  };
  enum : uint8_t { SEG_ROLL_CALL = 0, SEG_COLOUR_WHEEL, SEG_EFFECT, SEG_FINALE };

  static constexpr float CROSSFADE_S = 1.2f;

  // Start from the top (called whenever the demo is selected).
  void reset(uint8_t n);

  // Render one frame into out[0..n).  Same units as Effects::render.
  void render(RGBf *out, uint8_t n, uint32_t dtMs, uint8_t speed);

  // Current segment, for logging / tests.
  int segment() const { return _seg; }
  static int segmentCount();
  static const Segment &segmentAt(int i);
  float segmentTime() const { return _t; }
  // True once per segment change (consumed by the caller that logs it).
  bool takeSegmentChanged() {
    bool c = _changed;
    _changed = false;
    return c;
  }
  // Total loop length at speed 50, seconds (for docs/tests).
  static float loopSeconds();

private:
  Effects _fx[2];
  uint8_t _active = 0;     // _fx index rendering the current segment
  int _seg = 0;
  float _t = 0;            // seconds into the current segment (speed-scaled)
  bool _nextStarted = false;
  bool _cued1 = false, _cued2 = false;
  bool _changed = true;
  uint8_t _n = 0;
  RGBf _buf[MAX_PIXELS];

  void startSegment(Effects &fx, int seg, uint8_t n);
  void renderSegment(int seg, Effects &fx, float t, RGBf *out, uint8_t n, uint32_t dtMs, uint8_t speed);
};
