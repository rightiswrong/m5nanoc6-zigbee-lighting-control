// =============================================================================
//  Show — the one copy of "what the light should be doing", shared between
//  loop() (rendering, button) and the radio task (Zigbee or Matter callbacks).
// =============================================================================
#pragma once
#include <Arduino.h>
#include "Renderer.h"   // ShowState

class ShowStore {
public:
  // Snapshot for rendering / publishing.
  ShowState get() {
    portENTER_CRITICAL(&_mux);
    ShowState s = _s;
    portEXIT_CRITICAL(&_mux);
    return s;
  }

  // Change the state (from any task). The lambda runs inside the critical
  // section, so keep it to plain assignments. Marks settings for saving.
  template <class F> void edit(F f) {
    portENTER_CRITICAL(&_mux);
    f(_s);
    portEXIT_CRITICAL(&_mux);
    _lastChange = millis();
    _dirty = true;
  }

  // Run f(state) under the lock without marking anything dirty.
  template <class F> void peek(F f) {
    portENTER_CRITICAL(&_mux);
    f(_s);
    portEXIT_CRITICAL(&_mux);
  }

  void load();                    // from NVS (namespace "spooky")
  void saveIfQuiet(uint32_t now); // debounced NVS write

private:
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  ShowState _s;
  volatile uint32_t _lastChange = 0;
  volatile bool _dirty = false;
};

extern ShowStore g_show;

// Identify request from the controller ("which one are you?"): blink until.
extern volatile uint32_t g_identifyUntil;
