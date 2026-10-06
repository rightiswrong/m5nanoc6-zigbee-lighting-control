// Bit-level simulation of the hardware the firmware talks to:
//   * a chain of P9813 chips clocked on CI rising edges
//   * the shared DI / loopback-sense node on G2 (10k series + 18k pull-down)
//   * millis()/delay() on a virtual clock
#pragma once
#include "Arduino.h"
#include <vector>
#include <deque>
#include <assert.h>
#include "../../firmware/NanoC6_SpookyLights/config.h"
#include "../../firmware/NanoC6_SpookyLights/P9813Chain.h"

enum class ChipModel { Consume, ShiftRegister };

struct SimChip {
  ChipModel model = ChipModel::Consume;
  int latency = 1;          // re-timing delay in clocks (0 or 1)
  // consume model
  enum { ARMED, CONSUMING, FORWARDING } st = FORWARDING;
  int zeros = 0, bits = 0;
  uint32_t shift = 0, latched = 0;
  bool pending = false, prev = false;
  RGB8 shown{0, 0, 0};
  // shift-register model
  std::deque<bool> fifo;

  bool clock(bool b) {
    if (model == ChipModel::ShiftRegister) {
      if (fifo.empty()) fifo.assign(32 + latency, false);
      fifo.push_back(b);
      bool o = fifo.front();
      fifo.pop_front();
      return o;
    }
    bool out, consumed = false;
    zeros = b ? 0 : zeros + 1;
    if (st == CONSUMING) {
      consumed = true;
      shift = (shift << 1) | b;
      if (++bits == 32) { latched = shift; pending = true; st = FORWARDING; }
      out = false;
    } else if (st == ARMED && b) {
      st = CONSUMING; shift = 1; bits = 1; out = false; consumed = true;
    } else {
      out = latency ? prev : b;
    }
    if (zeros >= 32) {
      if (pending) {
        // word layout is flag | B | G | R  → R is the low byte
        shown = {(uint8_t)(latched & 0xFF), (uint8_t)(latched >> 8), (uint8_t)(latched >> 16)};
        pending = false;
      }
      st = ARMED;
    }
    prev = consumed ? false : b;   // a chip never forwards its own word
    return out;
  }
};

struct Sim {
  std::vector<SimChip> chips;
  bool loopback = true;
  bool stuckHigh = false;
  uint8_t dataMode = OUTPUT, dataLevel = LOW, clkLevel = LOW;
  bool lastDO = false;
  uint64_t clockEdges = 0;
  uint32_t nowMs = 0;
  uint64_t nowUs = 0;
  RGB8 onboard{0, 0, 0};
  int onboardWrites = 0;
  uint8_t buttonLevel = HIGH;

  void setChain(int n, ChipModel m = ChipModel::Consume, int latency = 1) {
    // Plugging/unplugging: keep existing chips' state, add fresh ones at the end
    std::vector<SimChip> next(n);
    for (int i = 0; i < n; ++i) {
      if (i < (int)chips.size()) next[i] = chips[i];
      next[i].model = m;
      next[i].latency = latency;
    }
    chips.swap(next);
    lastDO = false;
  }
  void risingEdge() {
    assert(dataMode == OUTPUT && "CI rose while G2 was released — module #1 would sample the loopback!");
    bool b = dataLevel;
    for (auto &c : chips) b = c.clock(b);
    lastDO = chips.empty() ? false : b;
    ++clockEdges;
  }
  int senseLevel() const {
    if (stuckHigh) return HIGH;
    if (dataMode == OUTPUT) return dataLevel;
    return (loopback && !chips.empty() && lastDO) ? HIGH : LOW;   // 18k pull-down otherwise
  }
};
extern Sim sim;

#ifdef SIM_IMPLEMENTATION
Sim sim;
HostSerial Serial;
HostESP ESP;
void pinMode(uint8_t pin, uint8_t mode) { if (pin == PIN_GROVE_DATA) sim.dataMode = mode; }
void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin == PIN_GROVE_DATA) sim.dataLevel = val;
  if (pin == PIN_GROVE_CLK) {
    if (val && !sim.clkLevel) sim.risingEdge();
    sim.clkLevel = val;
  }
}
int digitalRead(uint8_t pin) {
  if (pin == PIN_GROVE_DATA) return sim.senseLevel();
  if (pin == PIN_BUTTON) return sim.buttonLevel;
  return LOW;
}
void delayMicroseconds(uint32_t us) { sim.nowUs += us; }
void delay(uint32_t ms) { sim.nowMs += ms; }
uint32_t millis() { return sim.nowMs; }
void rgbLedWrite(uint8_t, uint8_t r, uint8_t g, uint8_t b) { sim.onboard = {r, g, b}; sim.onboardWrites++; }
#endif
