#include "P9813Chain.h"
#include <Arduino.h>

// Direction flips happen on every probed bit. On the ESP32 core 3.x pinMode()
// goes through the peripheral manager (several us each), so on target we use
// the ESP-IDF GPIO driver directly. The pin was claimed with pinMode() in
// begin(), so the Arduino layer still owns it as a plain GPIO; only the
// direction bit changes here and the output latch keeps its level.
#if defined(ARDUINO_ARCH_ESP32)
#include "driver/gpio.h"
static inline void lineRelease(uint8_t pin) { gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT); }
static inline void lineDrive(uint8_t pin) { gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT); }
static inline bool lineRead(uint8_t pin) { return gpio_get_level((gpio_num_t)pin) != 0; }
#else
static inline void lineRelease(uint8_t pin) { pinMode(pin, INPUT); }
static inline void lineDrive(uint8_t pin) { pinMode(pin, OUTPUT); }
static inline bool lineRead(uint8_t pin) { return digitalRead(pin) == HIGH; }
#endif

void P9813Chain::begin(uint8_t clkPin, uint8_t dataPin, uint16_t halfPeriodUs, uint16_t settleUs) {
  _clk = clkPin;
  _data = dataPin;
  _half = halfPeriodUs;
  _settle = settleUs;
  pinMode(_clk, OUTPUT);
  pinMode(_data, OUTPUT);
  digitalWrite(_clk, LOW);
  digitalWrite(_data, LOW);
}

uint32_t P9813Chain::encode(const RGB8 &c) {
  // Flag byte: 1 1 ~B7 ~B6 ~G7 ~G6 ~R7 ~R6  (the chip rejects words whose
  // checksum doesn't match — a nice built-in guard against cable noise).
  uint8_t flag = 0xC0;
  flag |= (uint8_t)((~c.b >> 6) & 0x03) << 4;
  flag |= (uint8_t)((~c.g >> 6) & 0x03) << 2;
  flag |= (uint8_t)((~c.r >> 6) & 0x03);
  return ((uint32_t)flag << 24) | ((uint32_t)c.b << 16) | ((uint32_t)c.g << 8) | c.r;
}

int P9813Chain::countFromFirstOneIndex(int firstOneBit) {
  if (firstOneBit < 0) return 0;                       // never saw a '1'
  if (firstOneBit < 32) return PROBE_LINE_STUCK_HIGH;   // high during start frame
  return (firstOneBit - 32) / 32;
}

// Data is set while CI is low and latched by the P9813 on CI's rising edge.
inline void P9813Chain::clockBit(bool bit) {
  digitalWrite(_data, bit ? HIGH : LOW);
  delayMicroseconds(_half);
  digitalWrite(_clk, HIGH);
  delayMicroseconds(_half);
  digitalWrite(_clk, LOW);
}

// Same as clockBit, then (with CI parked low so module #1 ignores DI) briefly
// release G2 and read what the last module is driving back to us.
inline bool P9813Chain::clockBitAndSense(bool bit) {
  clockBit(bit);
  lineRelease(_data);
  delayMicroseconds(_settle);
  bool seen = lineRead(_data);
  lineDrive(_data);
  return seen;
}

void P9813Chain::sendWord(uint32_t w) {
  for (int i = 31; i >= 0; --i) clockBit((w >> i) & 1u);
}

// End frame is padded to 64 zero bits: if each module re-times the stream by
// one clock, module k receives the end frame k bits late, and a bare 32-bit
// end frame would leave the far end of a long chain one colour behind.
void P9813Chain::endFrame() {
  sendWord(0);
  sendWord(0);
  digitalWrite(_data, LOW);
}

void P9813Chain::write(const RGB8 *px, uint8_t count) {
  sendWord(0);                                   // start frame
  for (uint8_t i = 0; i < count; ++i) sendWord(encode(px[i]));
  endFrame();
}

int P9813Chain::probe(const RGB8 *px, uint8_t count, uint8_t frames) {
  const RGB8 black{0, 0, 0};
  int bitIndex = 0;
  int firstOne = -1;

  // Flush: push one word of zeros per possible module through the chain so
  // nothing from the previous frame can still be in flight (matters if a chip
  // behaves as a 32-bit delay line). It simply reads as a long start frame.
  for (uint8_t w = 0; w < frames; ++w) sendWord(0);

  // Start frame (w = 0) + `frames` colour words. We sense after every bit
  // until the first '1' comes home, then finish the frame at full speed.
  for (int w = 0; w <= frames; ++w) {
    uint32_t word = 0;
    if (w > 0) word = encode((w - 1) < count ? px[w - 1] : black);
    for (int i = 31; i >= 0; --i, ++bitIndex) {
      bool bit = (word >> i) & 1u;
      if (firstOne < 0) {
        if (clockBitAndSense(bit)) firstOne = bitIndex;
      } else {
        clockBit(bit);
      }
    }
  }
  endFrame();
  return countFromFirstOneIndex(firstOne);
}
