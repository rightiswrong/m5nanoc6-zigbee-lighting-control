// Host-side stand-in for the Arduino-ESP32 API — just enough to compile and
// exercise the firmware logic on a PC. The GPIO functions drive a bit-level
// simulation of a Grove P9813 chain with the loopback sense wire (sim.h).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
void delayMicroseconds(uint32_t us);
void delay(uint32_t ms);
uint32_t millis();
void rgbLedWrite(uint8_t pin, uint8_t r, uint8_t g, uint8_t b);

struct HostSerial {
  void begin(unsigned long) {}
  void println(const char *s = "") { if (verbose) printf("%s\n", s); }
  int printf(const char *fmt, ...) {
    if (!verbose) return 0;
    va_list ap; va_start(ap, fmt); int n = vprintf(fmt, ap); va_end(ap); return n;
  }
  bool verbose = true;
};
extern HostSerial Serial;

struct HostESP { void restart() { printf("ESP.restart()\n"); } };
extern HostESP ESP;

// FreeRTOS critical sections → no-ops on the host
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
