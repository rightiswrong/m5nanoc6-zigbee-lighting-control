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
  static std::map<std::string, int> &kv() { static std::map<std::string, int> m; return m; }
};
