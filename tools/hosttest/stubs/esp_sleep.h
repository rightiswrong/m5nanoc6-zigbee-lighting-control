// Host stand-in for esp_sleep.h. Deep sleep never returns on the chip; here it
// throws HostDeepSleep so a test can catch it and simulate the next wake.
#pragma once
#include <stdint.h>
typedef enum { ESP_SLEEP_WAKEUP_UNDEFINED = 0, ESP_SLEEP_WAKEUP_TIMER = 4 } esp_sleep_wakeup_cause_t;
struct HostDeepSleep { uint64_t us; };
inline esp_sleep_wakeup_cause_t g_hostWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
inline uint64_t g_hostTimerUs = 0;
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return g_hostWakeCause; }
inline int esp_sleep_enable_timer_wakeup(uint64_t us) { g_hostTimerUs = us; return 0; }
[[noreturn]] inline void esp_deep_sleep_start() { throw HostDeepSleep{g_hostTimerUs}; }
