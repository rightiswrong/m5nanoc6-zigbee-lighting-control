#include "Show.h"
#include <Preferences.h>
#include "config.h"

ShowStore g_show;
volatile uint32_t g_identifyUntil = 0;

void ShowStore::load() {
  Preferences prefs;
  prefs.begin("spooky", true);
  ShowState s;
  s.on = prefs.getBool("on", s.on);
  s.level = prefs.getUChar("lvl", s.level);
  s.color.r = prefs.getUChar("r", s.color.r);
  s.color.g = prefs.getUChar("g", s.color.g);
  s.color.b = prefs.getUChar("b", s.color.b);
  s.effect = prefs.getUChar("fx", s.effect);
  s.speed = prefs.getUChar("spd", s.speed);
  s.output = prefs.getUChar("out", s.output);
  s.spotlight = prefs.getUChar("spot", s.spotlight);
  s.manualCount = prefs.getUChar("man", s.manualCount);
  prefs.end();
  if (s.effect >= FX_COUNT) s.effect = FX_CANDLE;
  if (s.output >= OUT_COUNT) s.output = OUT_AUTO;
  if (s.speed < 1 || s.speed > 100) s.speed = 50;
  if (s.manualCount > MAX_CHAIN) s.manualCount = 0;
  if (s.spotlight > MAX_PIXELS) s.spotlight = 0;
  portENTER_CRITICAL(&_mux);
  _s = s;
  portEXIT_CRITICAL(&_mux);
}

void ShowStore::saveIfQuiet(uint32_t now) {
  if (!_dirty || now - _lastChange < SETTINGS_SAVE_DELAY_MS) return;
  if (_savedOnce && now - _lastSave < SETTINGS_SAVE_MIN_INTERVAL_MS) return;   // stays dirty, saved later
  _dirty = false;
  _savedOnce = true;
  _lastSave = now;
  ++_saves;
  ShowState s = get();
  Preferences prefs;
  prefs.begin("spooky", false);
  prefs.putBool("on", s.on);
  prefs.putUChar("lvl", s.level);
  prefs.putUChar("r", s.color.r);
  prefs.putUChar("g", s.color.g);
  prefs.putUChar("b", s.color.b);
  prefs.putUChar("fx", s.effect);
  prefs.putUChar("spd", s.speed);
  prefs.putUChar("out", s.output);
  prefs.putUChar("spot", s.spotlight);
  prefs.putUChar("man", s.manualCount);
  prefs.end();
  Serial.println("[nvs] settings saved");
}
