#ifndef EE02_FIVE_DAY_WINDOW_H
#define EE02_FIVE_DAY_WINDOW_H
#include "five_day_snapshot.h"
#include "five_day_local_time.h"
#include <climits>

// Pure construction from caller-trusted UTC and caller-configured POSIX runtime.
// Caller must call tzset(), provide readable strings and exclude all concurrent
// TZ/tzset changes. This verifies observed context, not arbitrary TZ rule syntax.
namespace FiveDayWindow {
enum class Status { Ok, InvalidArgument, UntrustedTime, MissingTimezone,
    TimezoneMismatch, ConversionFailure, UnsupportedLocalMidnight, InvalidWindow };
namespace detail {
inline Status context(const char* configured) {
    if(!configured || !*configured || !std::getenv("TZ")) return Status::MissingTimezone;
    return FiveDayLocalTime::matches(configured)?Status::Ok:Status::TimezoneMismatch;
}
inline bool midnight(const struct tm& t,const FiveDayLocalTime::Date& date) {
    return int64_t(t.tm_year)+1900==date.year && t.tm_mon==int(date.month)-1 &&
        t.tm_mday==int(date.day) && !t.tm_hour && !t.tm_min && !t.tm_sec;
}
inline Status boundary(int32_t day,const char* zone,int64_t& output) {
    const auto date=FiveDayLocalTime::date(day);
    if(date.year-1900<INT_MIN || date.year-1900>INT_MAX) return Status::ConversionFailure;
    bool found=false; int64_t earliest=0;
    const int guesses[]={-1,0,1};
    for(int isdst:guesses) {
        auto state=context(zone); if(state!=Status::Ok) return state;
        struct tm requested={}; requested.tm_year=int(date.year-1900);
        requested.tm_mon=int(date.month)-1; requested.tm_mday=int(date.day); requested.tm_isdst=isdst;
        const time_t stamp=std::mktime(&requested);
        state=context(zone); if(state!=Status::Ok) return state;
        int64_t utc=0; struct tm roundtrip={};
        // -1 can be a valid pre-epoch instant. Only exact round-trip evidence
        // admits a candidate; normalized dates or non-midnight times are ignored.
        const bool readable=FiveDayLocalTime::fromStamp(stamp,utc) && FiveDayLocalTime::read(utc,roundtrip);
        state=context(zone); if(state!=Status::Ok) return state;
        if(!readable || !midnight(roundtrip,date)) continue;
        // Equivalent candidates have no additional effect; choose earliest UTC.
        if(!found || utc<earliest) { earliest=utc; found=true; }
    }
    if(!found) return Status::UnsupportedLocalMidnight;
    output=earliest; return Status::Ok;
}
}
inline Status build(int64_t nowUtc,bool clockTrusted,const char* configuredPosixTimezone,FiveDaySnapshot::Window& output) {
    if(!clockTrusted) return Status::UntrustedTime;
    using namespace detail;
    auto state=context(configuredPosixTimezone); if(state!=Status::Ok) return state;
    struct tm today={};
    const bool readable=FiveDayLocalTime::read(nowUtc,today);
    state=context(configuredPosixTimezone); if(state!=Status::Ok) return state;
    if(!readable) return Status::ConversionFailure;
    const int64_t first=FiveDayLocalTime::civilDay(int64_t(today.tm_year)+1900,unsigned(today.tm_mon+1),unsigned(today.tm_mday));
    if(first<INT32_MIN || first>INT32_MAX-5) return Status::InvalidWindow;
    FiveDaySnapshot::Window candidate={}; candidate.firstCivilDay=int32_t(first);
    for(size_t i=0;i<6;++i) {
        state=boundary(int32_t(first+int64_t(i)),configuredPosixTimezone,candidate.utcBoundaries[i]);
        if(state!=Status::Ok) return state;
    }
    for(size_t i=0;i<6;++i) {
        const int64_t utc=candidate.utcBoundaries[i];
        if(i && utc<=candidate.utcBoundaries[i-1]) return Status::InvalidWindow;
        FiveDayLocalTime::Value local={};
        if(!FiveDayLocalTime::convert(utc,local,configuredPosixTimezone)) {
            state=context(configuredPosixTimezone);
            return state==Status::Ok?Status::ConversionFailure:state;
        }
        if(local.civilDay!=first+int64_t(i) || local.hour || local.minute || local.second) return Status::InvalidWindow;
        if(i) {
            if(utc==INT64_MIN) return Status::InvalidWindow;
            if(!FiveDayLocalTime::convert(utc-1,local,configuredPosixTimezone)) {
                state=context(configuredPosixTimezone);
                return state==Status::Ok?Status::ConversionFailure:state;
            }
            if(local.civilDay!=first+int64_t(i)-1) return Status::InvalidWindow;
        }
    }
    if(nowUtc<candidate.utcBoundaries[0] || nowUtc>=candidate.utcBoundaries[1]) return Status::InvalidWindow;
    state=context(configuredPosixTimezone); if(state!=Status::Ok) return state;
    output=candidate; return Status::Ok;
}
}
#endif
