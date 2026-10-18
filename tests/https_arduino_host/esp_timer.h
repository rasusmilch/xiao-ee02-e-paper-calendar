#ifndef EE02_TEST_ESP_TIMER_H
#define EE02_TEST_ESP_TIMER_H
#include <cstdint>
extern int64_t testEspMicroseconds;
inline int64_t esp_timer_get_time() { return testEspMicroseconds; }
#endif
