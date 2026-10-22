#include "MatterCodes.h"
#include <string.h>
#include "spookyqr.h"

namespace mcodes {

bool passcodeValid(uint32_t p) {
  if (p < 1 || p > 99999998u) return false;
  if (p == 12345678u || p == 87654321u) return false;
  if (p % 11111111u == 0) return false;          // 11111111 … 88888888 (99999999 is out of range)
  return true;
}

uint32_t uniformBelow(uint32_t (*rnd)(), uint32_t n) {
  // Rejection sampling: drop the top partial copy of [0, n) so every value is
  // equally likely (plain rnd() % n favours small values slightly).
  const uint32_t limit = UINT32_MAX - (UINT32_MAX % n);   // a multiple of n
  uint32_t x;
  do {
    x = rnd();
  } while (x >= limit);
  return x % n;
}

void generateSetup(uint32_t (*rnd)(), uint32_t &passcode, uint16_t &discriminator) {
  do {
    passcode = 1u + uniformBelow(rnd, 99999998u);           // 1 … 99999998
  } while (!passcodeValid(passcode));
  discriminator = (uint16_t)(rnd() & 0x0FFFu);
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool payloadFromUrl(const char *url, char *out, size_t outLen) {
  if (!url || !out || outLen == 0) return false;
  const char *p = strstr(url, "data=");
  if (!p) return false;
  p += 5;
  size_t n = 0;
  while (*p && *p != '&' && *p != '#') {
    char c = *p++;
    if (c == '%' && hexval(p[0]) >= 0 && hexval(p[1]) >= 0) {
      c = (char)(hexval(p[0]) * 16 + hexval(p[1]));
      p += 2;
    }
    if (n + 1 >= outLen) return false;
    out[n++] = c;
  }
  out[n] = '\0';
  return n > 3 && strncmp(out, "MT:", 3) == 0;
}

void formatManualCode(const char *code, char *out, size_t outLen) {
  if (!out || outLen == 0) return;
  size_t len = code ? strlen(code) : 0;
  if (len == 11 && outLen >= 14) {
    memcpy(out, code, 4);
    out[4] = '-';
    memcpy(out + 5, code + 4, 3);
    out[8] = '-';
    memcpy(out + 9, code + 7, 4);
    out[13] = '\0';
    return;
  }
  size_t n = len < outLen - 1 ? len : outLen - 1;
  if (n) memcpy(out, code, n);
  out[n] = '\0';
}

uint8_t bandToLevel(uint8_t v) {
  if (v > BAND_MAX_VALUE) v = BAND_MAX_VALUE;
  return (uint8_t)(BAND_OFFSET + BAND_STEP * v);
}

uint8_t levelToBand(uint8_t level) {
  if (level <= BAND_OFFSET) return 0;
  unsigned v = (unsigned)(level - BAND_OFFSET + BAND_STEP / 2) / BAND_STEP;   // round to nearest
  return (uint8_t)(v > BAND_MAX_VALUE ? BAND_MAX_VALUE : v);
}

uint8_t fineToLevel(uint8_t v) {
  if (v > FINE_MAX_VALUE) v = FINE_MAX_VALUE;
  return (uint8_t)(FINE_OFFSET + FINE_STEP * v);
}

uint8_t levelToFine(uint8_t level) {
  if (level <= FINE_OFFSET) return 0;
  unsigned v = (unsigned)(level - FINE_OFFSET + FINE_STEP / 2) / FINE_STEP;   // round half up
  return (uint8_t)(v > FINE_MAX_VALUE ? FINE_MAX_VALUE : v);
}

uint8_t speedToLevel(uint8_t speed) {
  if (speed < 1) speed = 1;
  if (speed > 100) speed = 100;
  unsigned l = (speed * 254u + 50u) / 100u;
  return (uint8_t)(l < 1 ? 1 : l);
}

uint8_t levelToSpeed(uint8_t level) {
  unsigned s = (level * 100u + 127u) / 254u;
  if (s < 1) s = 1;
  if (s > 100) s = 100;
  return (uint8_t)s;
}

bool printQr(const char *text, bool darkTerminal, void (*emit)(const char *line)) {
  // Matter payloads are ~22 characters → version 2 at ECC M. Allow up to 6.
  static uint8_t qr[spookyqr_BUFFER_LEN_FOR_VERSION(6)];
  static uint8_t tmp[spookyqr_BUFFER_LEN_FOR_VERSION(6)];
  if (!spookyqr_encodeText(text, tmp, qr, spookyqr_Ecc_MEDIUM, 1, 6, spookyqr_Mask_AUTO, true)) return false;

  const int size = spookyqr_getSize(qr);
  const int quiet = 2;
  static const char FULL[] = "\xE2\x96\x88", UPPER[] = "\xE2\x96\x80", LOWER[] = "\xE2\x96\x84";
  // Each text column is one module; each text line is two module rows.
  char line[(6 * 4 + 17 + 4) * 3 + 1];
  for (int y = -quiet; y < size + quiet; y += 2) {
    size_t n = 0;
    for (int x = -quiet; x < size + quiet; ++x) {
      // getModule() returns false (light) outside the symbol → quiet zone.
      bool topDark = spookyqr_getModule(qr, x, y);
      bool botDark = spookyqr_getModule(qr, x, y + 1);
      bool botExists = y + 1 < size + quiet;      // odd row count: last line is half empty
      // "ink" = what we draw with block characters.
      bool topInk = darkTerminal ? !topDark : topDark;
      bool botInk = botExists && (darkTerminal ? !botDark : botDark);
      const char *glyph = topInk && botInk ? FULL : topInk ? UPPER : botInk ? LOWER : " ";
      size_t g = strlen(glyph);
      memcpy(line + n, glyph, g);
      n += g;
    }
    line[n] = '\0';
    emit(line);
  }
  return true;
}

}  // namespace mcodes
