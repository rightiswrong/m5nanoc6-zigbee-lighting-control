// Host stand-in for driver/gpio.h pad hold (records which pins are held).
#pragma once
#include <set>
typedef int gpio_num_t;
inline std::set<int> g_hostHeld;
inline bool g_hostDeepSleepHold = false;
inline int gpio_hold_en(gpio_num_t p) { g_hostHeld.insert(p); return 0; }
inline int gpio_hold_dis(gpio_num_t p) { g_hostHeld.erase(p); return 0; }
inline void gpio_deep_sleep_hold_en() { g_hostDeepSleepHold = true; }
