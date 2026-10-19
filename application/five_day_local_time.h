#ifndef EE02_FIVE_DAY_LOCAL_TIME_H
#define EE02_FIVE_DAY_LOCAL_TIME_H
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

// Shared civil arithmetic and narrow POSIX conversion for window/renderer use.
// Caller configures TZ/tzset first and keeps the expected string and runtime
// timezone immutable throughout each call. No clock acquisition or TZ mutation.
namespace FiveDayLocalTime {
struct Value { int32_t civilDay; uint8_t hour, minute, second; int32_t offsetSeconds; };
struct Date { int64_t year; unsigned month, day; };
// Inputs come from checked tm fields; intermediate years remain within tm's int
// range plus 1900. Civil ordinals/dates use the proleptic Gregorian convention.
inline int64_t civilDay(int64_t year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned y = static_cast<unsigned>(year - era * 400);
    const unsigned m = month > 2 ? month - 3 : month + 9;
    return era * 146097 + y * 365 + y / 4 - y / 100 + (153 * m + 2) / 5 + day - 1 - 719468;
}
inline Date date(int32_t day) {
    const int64_t z = int64_t(day) + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    return {y,m,d};
}
inline bool matches(const char* configured) {
    const char* actual=std::getenv("TZ");
    return configured && *configured && actual && !std::strcmp(actual,configured);
}
inline bool toStamp(int64_t utc,time_t& stamp) {
    if (!std::numeric_limits<time_t>::is_signed && utc<0) return false;
    if (std::numeric_limits<time_t>::digits<63 &&
        (utc<static_cast<int64_t>(std::numeric_limits<time_t>::min()) ||
         utc>static_cast<int64_t>(std::numeric_limits<time_t>::max()))) return false;
    stamp=static_cast<time_t>(utc);
    return true;
}
inline bool fromStamp(time_t stamp,int64_t& utc) {
    if (std::numeric_limits<time_t>::digits>63 &&
        (stamp>static_cast<time_t>(INT64_MAX) ||
         (std::numeric_limits<time_t>::is_signed && stamp<static_cast<time_t>(INT64_MIN)))) return false;
    utc=static_cast<int64_t>(stamp); return true;
}
inline bool fields(const struct tm& local) {
    if(local.tm_mon<0 || local.tm_mon>11 || local.tm_mday<1 || local.tm_mday>31 ||
       local.tm_hour<0 || local.tm_hour>23 || local.tm_min<0 || local.tm_min>59 || local.tm_sec<0 || local.tm_sec>59) return false;
    const int64_t year=int64_t(local.tm_year)+1900;
    const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap=year%4==0 && (year%100!=0 || year%400==0);
    return unsigned(local.tm_mday)<=days[local.tm_mon]+unsigned(local.tm_mon==1 && leap);
}
inline bool read(int64_t utc,struct tm& local) {
    time_t stamp={}; return toStamp(utc,stamp) && localtime_r(&stamp,&local) && fields(local);
}
inline bool convert(int64_t utc,Value& out,const char* configured) {
    if(!matches(configured)) return false;
    struct tm local={};
    if(!read(utc,local) || !matches(configured)) return false;
    const int64_t day=civilDay(int64_t(local.tm_year)+1900,unsigned(local.tm_mon+1),unsigned(local.tm_mday));
    if(day<INT32_MIN || day>INT32_MAX) return false;
    const int64_t wall=day*86400+local.tm_hour*3600+local.tm_min*60+local.tm_sec;
    if((utc<0 && wall>INT64_MAX+utc) || (utc>0 && wall<INT64_MIN+utc)) return false;
    const int64_t offset=wall-utc;
    if(offset<INT32_MIN || offset>INT32_MAX) return false;
    out={int32_t(day),uint8_t(local.tm_hour),uint8_t(local.tm_min),uint8_t(local.tm_sec),int32_t(offset)};
    return true;
}
}
#endif
