// =============================================================================
//  P9813Chain — bit-banged driver for Grove Chainable RGB LED v2.0 (P9813)
//  with *live module counting* over a single shared data/sense wire.
// =============================================================================
//
//  Why counting needs a return wire
//  --------------------------------
//  A P9813 chain is write-only: data goes in at CI/DI and is re-timed out of
//  CO/DO to the next module. Nothing ever comes back to the controller, so the
//  MCU cannot ask "how many of you are there?". To count, we route the LAST
//  module's DO back to the controller ("loopback") and measure how long a known
//  bit pattern takes to emerge.
//
//  The NanoC6 only exposes two Grove signals, both already used (CI, DI), so
//  the loopback shares G2 with DI through a resistor divider:
//
//      G2 ──┬──────────────────────────► module #1 DI
//           ├── 18k ── GND                (pull-down + lower divider leg)
//           └── 10k ── return wire ◄──── last module DO (5 V logic)
//
//  When G2 drives, the 10k keeps the returning signal from fighting it.
//  When G2 is released (input), it reads DO scaled to 5 V*18/28 = 3.2 V.
//  Module #1 only samples DI on a rising CI edge, and we only release G2 while
//  CI is held low, so the time-sharing is invisible to the chain.
//
//  The counting math (model-independent)
//  -------------------------------------
//  A frame is: 32 zero bits (start) + one 32-bit word per LED + 32 zero bits.
//  Every LED word begins with the flag bits "11". Each module delays the
//  stream by exactly one 32-bit word before passing data on (whether it
//  "consumes" its own word or acts as a 32-bit shift register, the delay is
//  the same). So the first '1' seen at the end of an N-module chain appears at
//  bit index   i = 32 (start) + 32*N + d,   where d is the re-timing latency
//  (0 or 1 clock per module). Therefore   N = (i - 32) / 32   (integer
//  division) holds as long as d < 32, i.e. for chains of up to 31 modules —
//  comfortably above MAX_CHAIN.
//
//  We send MAX_CHAIN+1 real colour words, so the probe doubles as a normal
//  refresh — no flash, no black frame.
// =============================================================================
#pragma once
#include <stdint.h>

struct RGB8 {
  uint8_t r, g, b;
};

class P9813Chain {
public:
  // Result codes for probe()
  static constexpr int PROBE_LINE_STUCK_HIGH = -1;

  void begin(uint8_t clkPin, uint8_t dataPin, uint16_t halfPeriodUs, uint16_t settleUs);

  // Plain refresh of `count` pixels (fast, no sensing).
  void write(const RGB8 *px, uint8_t count);

  // Refresh + count. Sends `frames` words (pixels beyond `count` are black)
  // and returns the number of modules detected (0..frames-1), or
  // PROBE_LINE_STUCK_HIGH if the sense line was already high during the start
  // frame (wiring fault: missing pull-down / divider, or DO wired directly).
  int probe(const RGB8 *px, uint8_t count, uint8_t frames);

  // Pure function used by probe(), exposed for unit tests.
  static int countFromFirstOneIndex(int firstOneBit);

  // Build the 32-bit P9813 word for one colour (flag + inverted checksum + B G R)
  static uint32_t encode(const RGB8 &c);

private:
  uint8_t _clk = 0, _data = 0;
  uint16_t _half = 3, _settle = 6;

  inline void clockBit(bool bit);
  inline bool clockBitAndSense(bool bit);
  void sendWord(uint32_t w);
  void endFrame();
};
