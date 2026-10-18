#pragma once
#include <map>
#include <string>
#include <stdint.h>
// In-memory NVS stand-in
class Preferences {
public:
  bool begin(const char *, bool = false) { return true; }
  void end() {}
  bool getBool(const char *k, bool d) { auto it = kv().find(k); return it == kv().end() ? d : it->second; }
  uint8_t getUChar(const char *k, uint8_t d) { auto it = kv().find(k); return it == kv().end() ? d : it->second; }
  size_t putBool(const char *k, bool v) { kv()[k] = v; return 1; }
  size_t putUChar(const char *k, uint8_t v) { kv()[k] = v; return 1; }
  uint32_t getUInt(const char *k, uint32_t d) { auto it = big().find(k); return it == big().end() ? d : it->second; }
  size_t putUInt(const char *k, uint32_t v) { big()[k] = v; return 4; }
  uint16_t getUShort(const char *k, uint16_t d) { auto it = big().find(k); return it == big().end() ? d : (uint16_t)it->second; }
  size_t putUShort(const char *k, uint16_t v) { big()[k] = v; return 2; }
  static std::map<std::string, uint32_t> &big() { static std::map<std::string, uint32_t> m; return m; }
  static std::map<std::string, int> &kv() { static std::map<std::string, int> m; return m; }
};
