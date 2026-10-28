#include "Power.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "config.h"

namespace {
esp_sleep_wakeup_cause_t g_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
// Pins that must stay low while asleep: the WS2812 load switch (an idle WS2812
// still draws ~1 mA), the blue status LED, and the Grove clock/data lines (a
// floating clock could shift garbage into the P9813s and light them).
const uint8_t HELD_PINS[] = {PIN_RGB_POWER, PIN_STATUS_LED, PIN_GROVE_CLK, PIN_GROVE_DATA};
}  // namespace

void power::begin() {
  g_cause = esp_sleep_get_wakeup_cause();
  for (uint8_t p : HELD_PINS) gpio_hold_dis((gpio_num_t)p);
#if CPU_MHZ
  if (getCpuFrequencyMhz() != CPU_MHZ && !setCpuFrequencyMhz(CPU_MHZ))
    Serial.printf("[power] CPU clock %u MHz not supported, staying at %u MHz\n", (unsigned)CPU_MHZ,
                  (unsigned)getCpuFrequencyMhz());
#endif
}

bool power::timerWake() { return g_cause == ESP_SLEEP_WAKEUP_TIMER; }

const char *power::wakeReason() {
  switch (g_cause) {
    case ESP_SLEEP_WAKEUP_TIMER: return "check-in";
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "power-on/reset";
    default: return "other";
  }
}

void power::deepSleep(uint32_t seconds) {
  for (uint8_t p : HELD_PINS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
    gpio_hold_en((gpio_num_t)p);
  }
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  gpio_deep_sleep_hold_en();
#endif
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  Serial.printf("[power] sleeping %lu s (awake %lu ms this time)\n", (unsigned long)seconds,
                (unsigned long)millis());
  Serial.flush();
  esp_deep_sleep_start();
}
