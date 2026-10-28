// =============================================================================
//  Power — CPU clock and deep sleep (battery build).
//
//  The ESP32-C6 has no low-power mode that keeps a Zigbee *router* online: a
//  router's receiver has to listen all the time. The battery build is a Zigbee
//  end device instead, and when the light is off it deep-sleeps (the chip draws
//  microamps), waking on a timer to check in. Arduino's Zigbee library has no
//  light-sleep support, so each check-in is a short reboot that rejoins the
//  network from flash.
// =============================================================================
#pragma once
#include <stdint.h>

namespace power {

// First thing in setup(): note why we woke, release pins held through sleep,
// set the CPU clock (config.h CPU_MHZ).
void begin();

// This boot is a scheduled check-in (timer wake from deep sleep).
bool timerWake();

// Human-readable reason for the log.
const char *wakeReason();

// Hold the LED pins low and deep-sleep for `seconds`. The caller blanks the
// LEDs first (the P9813 modules latch their last colour while powered).
[[noreturn]] void deepSleep(uint32_t seconds);

}  // namespace power
