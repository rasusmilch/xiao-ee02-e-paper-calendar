#ifndef EE02_FIVE_DAY_POSIX_TIME_H
#define EE02_FIVE_DAY_POSIX_TIME_H
#include "five_day_renderer.h"

// Narrow platform adapter: caller explicitly sets TZ and calls tzset() before
// use. A mismatched/missing TZ fails rather than silently using ambient time.
// No concurrent TZ changes; context string must remain readable during render.
namespace FiveDayPosixTime {
struct Context { const char* configuredTimezone; };
inline bool convert(int64_t utc, FiveDayRenderer::LocalTime& out, void* context) {
    if (!context) return false;
    const auto& c = *static_cast<const Context*>(context);
    return FiveDayLocalTime::convert(utc,out,c.configuredTimezone);
}
}
#endif
