#pragma once
// Host stand-in for ESP-IDF bootloader_support/bootloader_random.h. The test
// RNG (sim.h) counts calls made while this "entropy source" is on.
extern int g_bootRandomOn;
inline void bootloader_random_enable() { ++g_bootRandomOn; }
inline void bootloader_random_disable() { --g_bootRandomOn; }
