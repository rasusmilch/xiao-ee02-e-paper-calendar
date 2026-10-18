#ifndef EE02_FIVE_DAY_POSIX_TIME_H
#define EE02_FIVE_DAY_POSIX_TIME_H
#include "five_day_renderer.h"
#include <ctime>
#include <limits>

// Narrow platform adapter: caller explicitly sets TZ and calls tzset() before
// use. A mismatched/missing TZ fails rather than silently using ambient time.
// No concurrent TZ changes; context string must remain readable during render.
namespace FiveDayPosixTime {
struct Context { const char* configuredTimezone; };
inline bool convert(int64_t utc, FiveDayRenderer::LocalTime& out, void* context) {
    if (!context) return false;
    const auto& c = *static_cast<const Context*>(context);
    const char* actual = std::getenv("TZ");
    if (!c.configuredTimezone || !*c.configuredTimezone || !actual || std::strcmp(actual, c.configuredTimezone)) return false;
    if (!std::numeric_limits<time_t>::is_signed && utc < 0) return false;
    const time_t stamp = static_cast<time_t>(utc);
    if (static_cast<int64_t>(stamp) != utc) return false;
    struct tm local = {};
    if (!localtime_r(&stamp, &local)) return false;
    const int64_t day = FiveDayRenderer::detail::civilDay(int64_t(local.tm_year) + 1900,
        static_cast<unsigned>(local.tm_mon + 1), static_cast<unsigned>(local.tm_mday));
    if (day < INT32_MIN || day > INT32_MAX || local.tm_hour < 0 || local.tm_hour > 23 ||
        local.tm_min < 0 || local.tm_min > 59 || local.tm_sec < 0 || local.tm_sec > 59) return false;
    const int64_t wall = day * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    // Avoid subtraction overflow for an unrepresentable supplied instant.
    if ((utc < 0 && wall > INT64_MAX + utc) || (utc > 0 && wall < INT64_MIN + utc)) return false;
    const int64_t offset = wall - utc;
    if (offset < INT32_MIN || offset > INT32_MAX) return false;
    out = {static_cast<int32_t>(day), static_cast<uint8_t>(local.tm_hour),
        static_cast<uint8_t>(local.tm_min), static_cast<uint8_t>(local.tm_sec), static_cast<int32_t>(offset)};
    return true;
}
}
#endif
