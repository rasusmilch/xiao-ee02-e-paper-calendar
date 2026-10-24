#ifndef EE02_ROOT_TEST_SLEEP_H
#define EE02_ROOT_TEST_SLEEP_H
#include <cstdint>
using esp_err_t=int;
static const esp_err_t ESP_OK=0,ESP_ERR_INVALID_ARG=0x102;
namespace SleepTrace {
struct Handoff{};
struct State {unsigned timerCalls=0,handoffs=0;uint64_t micros=0,maxMicros=UINT64_MAX;
    esp_err_t timerResult=ESP_OK;void (*check)()=nullptr;};
inline State& state(){static State value;return value;}
}
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us){auto& s=SleepTrace::state();
    if(s.check)s.check();
    ++s.timerCalls;s.micros=us;
    return !us || us>s.maxMicros?ESP_ERR_INVALID_ARG:s.timerResult;}
[[noreturn]] inline void esp_deep_sleep_start(){auto& s=SleepTrace::state();if(s.check)s.check();
    ++s.handoffs;throw SleepTrace::Handoff();} // Simulated nonreturn, outside all borrowed scopes.
#endif
