#ifndef EE02_ICS_RECUR_VALUE_H
#define EE02_ICS_RECUR_VALUE_H

#include "ics_typed_values.h"

// RFC 5545 3.3.10 core representation only, not recurrence application or source
// acceptance. Input must remain readable and unchanged for the call. Arithmetic
// span checks cannot prove storage ownership. No borrowed reference escapes.
namespace IcsRecurValue {
enum class Status { CoreGrammarValid, InvalidArgument, MalformedRule, UnsupportedFeature, ResourceLimit };
enum class Frequency { Secondly, Minutely, Hourly, Daily, Weekly, Monthly, Yearly, Unknown };
enum Presence : uint32_t {
    FREQ = 1U << 0, UNTIL = 1U << 1, COUNT = 1U << 2, INTERVAL = 1U << 3,
    BYSECOND = 1U << 4, BYMINUTE = 1U << 5, BYHOUR = 1U << 6, BYDAY = 1U << 7,
    BYMONTHDAY = 1U << 8, BYYEARDAY = 1U << 9, BYWEEKNO = 1U << 10,
    BYMONTH = 1U << 11, BYSETPOS = 1U << 12, WKST = 1U << 13
};
struct Limits { size_t maxRuleBytes, maxParts, maxListItems; };
struct Report {
    Status status;
    size_t offset;
    Frequency frequency;
    uint32_t presence;
    bool leapSecondPending;
};

namespace detail {
using View = IcsStructure::View;
enum class Id { Freq, Until, Count, Interval, BySecond, ByMinute, ByHour, ByDay,
    ByMonthDay, ByYearDay, ByWeekNo, ByMonth, BySetPos, Wkst, Extension };
inline Report result(Status s, size_t offset = 0) { return {s, offset, Frequency::Unknown, 0, false}; }
inline bool digit(uint8_t c) { return c >= '0' && c <= '9'; }
inline bool nameChar(uint8_t c) {
    return digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}
inline Id identify(View name) {
    const char* const names[] = {"FREQ", "UNTIL", "COUNT", "INTERVAL", "BYSECOND", "BYMINUTE",
        "BYHOUR", "BYDAY", "BYMONTHDAY", "BYYEARDAY", "BYWEEKNO", "BYMONTH", "BYSETPOS", "WKST"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (IcsStructure::identifier(name, names[i])) return static_cast<Id>(i);
    // RSCALE, SKIP and every other well-framed unrecognized name all remain
    // unsupported. No extension grammar (including its values) is claimed here.
    return Id::Extension;
}
inline uint32_t bit(Id id) { return id == Id::Extension ? 0 : uint32_t(1) << static_cast<unsigned>(id); }
inline bool list(Id id) { return id >= Id::BySecond && id <= Id::BySetPos; }
struct Part { size_t start, value, end; Id id; };
inline size_t next(const Part& p, size_t size) { return p.end == size ? size : p.end + 1; }
// Framing pass: names follow iCalendar token characters; values are nonempty
// opaque spans until core decoding. A second '=' is not a NAME=VALUE separator.
// No trimming, extension grammar validation or Gregorian assumptions occur here.
inline Report part(View v, size_t start, Part& p) {
    size_t i = start;
    while (i < v.size && v.data[i] != '=' && v.data[i] != ';') {
        if (!nameChar(v.data[i])) return result(Status::MalformedRule, i);
        ++i;
    }
    if (i == start || i == v.size || v.data[i] != '=') return result(Status::MalformedRule, i);
    p = {start, i + 1, i + 1, identify({v.data + start, i - start})};
    while (p.end < v.size && v.data[p.end] != ';') {
        if (v.data[p.end] == '=') return result(Status::MalformedRule, p.end);
        ++p.end;
    }
    if (p.value == p.end) return result(Status::MalformedRule, p.value);
    if (p.end < v.size && p.end + 1 == v.size) return result(Status::MalformedRule, v.size);
    return result(Status::CoreGrammarValid);
}
inline Frequency frequency(View v) {
    const char* const names[] = {"SECONDLY", "MINUTELY", "HOURLY", "DAILY", "WEEKLY", "MONTHLY", "YEARLY"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (IcsStructure::identifier(v, names[i])) return static_cast<Frequency>(i);
    return Frequency::Unknown;
}
inline bool weekday(View v) {
    return IcsStructure::identifier(v, "SU") || IcsStructure::identifier(v, "MO") ||
        IcsStructure::identifier(v, "TU") || IcsStructure::identifier(v, "WE") ||
        IcsStructure::identifier(v, "TH") || IcsStructure::identifier(v, "FR") ||
        IcsStructure::identifier(v, "SA");
}
inline Report positive(View v, const Part& p) {
    uint64_t n = 0;
    for (size_t i = p.value; i < p.end; ++i) {
        if (!digit(v.data[i])) return result(Status::MalformedRule, i);
        const unsigned d = v.data[i] - '0';
        if (n > (UINT64_MAX - d) / 10) return result(Status::ResourceLimit, i);
        n = n * 10 + d;
    }
    return result(n ? Status::CoreGrammarValid : Status::MalformedRule, p.value);
}
inline Report number(View v, size_t start, size_t end, bool signedValue,
                     unsigned width, unsigned minimum, unsigned maximum, unsigned& out) {
    size_t i = start;
    if (i < end && signedValue && (v.data[i] == '+' || v.data[i] == '-')) ++i;
    const size_t digits = i;
    unsigned n = 0;
    for (; i < end; ++i) {
        if (!digit(v.data[i]) || i - digits == width) return result(Status::MalformedRule, i);
        n = n * 10 + v.data[i] - '0'; // At most three digits, bounded before accumulation.
    }
    if (i == digits || n < minimum || n > maximum) return result(Status::MalformedRule, start);
    out = n; return result(Status::CoreGrammarValid);
}
inline Report member(View v, Id id, size_t start, size_t end, size_t& ordinal, bool& leap) {
    if (id == Id::ByDay) {
        if (end - start < 2 || !weekday({v.data + end - 2, 2}))
            return result(Status::MalformedRule, end - start < 2 ? start : end - 2);
        if (end - start == 2) return result(Status::CoreGrammarValid);
        unsigned n = 0;
        const Report r = number(v, start, end - 2, true, 2, 1, 53, n);
        if (r.status == Status::CoreGrammarValid && ordinal == v.size) ordinal = start;
        return r;
    }
    const bool sign = id == Id::ByMonthDay || id == Id::ByYearDay || id == Id::ByWeekNo || id == Id::BySetPos;
    const unsigned width = id == Id::ByYearDay || id == Id::BySetPos ? 3 : 2;
    const unsigned minimum = id == Id::BySecond || id == Id::ByMinute || id == Id::ByHour ? 0 : 1;
    unsigned maximum = 0;
    switch (id) {
    case Id::BySecond: maximum = 60; break; case Id::ByMinute: maximum = 59; break;
    case Id::ByHour: maximum = 23; break; case Id::ByMonthDay: maximum = 31; break;
    case Id::ByYearDay: case Id::BySetPos: maximum = 366; break;
    case Id::ByWeekNo: maximum = 53; break; case Id::ByMonth: maximum = 12; break;
    default: return result(Status::MalformedRule, start);
    }
    unsigned n = 0;
    const Report r = number(v, start, end, sign, width, minimum, maximum, n);
    if (r.status == Status::CoreGrammarValid && id == Id::BySecond && n == 60) leap = true;
    return r;
}
}

inline Report validate(IcsStructure::View value, const Limits& limits) {
    using namespace detail;
    if (!ee02_ics_detail::span(value.data, value.size)) return result(Status::InvalidArgument);
    if (value.size > limits.maxRuleBytes) return result(Status::ResourceLimit, limits.maxRuleBytes);
    if (!value.size) return result(Status::MalformedRule);
    uint32_t presence = 0;
    size_t parts = 0, items = 0, extension = value.size;
    // Pass 1: complete framing, core uniqueness/presence and structural budgets.
    // This precedes extension disposition, regardless of extension position.
    for (size_t i = 0; i < value.size;) {
        if (parts == limits.maxParts) return result(Status::ResourceLimit, i);
        ++parts; // Each part consumes input; admitted spans are <= PTRDIFF_MAX.
        Part p = {};
        Report r = part(value, i, p);
        if (r.status != Status::CoreGrammarValid) return r;
        const uint32_t b = bit(p.id);
        if (presence & b) return result(Status::MalformedRule, p.start);
        presence |= b;
        if (p.id == Id::Extension && extension == value.size) extension = p.start;
        if (list(p.id)) {
            size_t start = p.value;
            for (size_t end = start;; ++end) if (end == p.end || value.data[end] == ',') {
                if (start == end) return result(Status::MalformedRule, start);
                if (items == limits.maxListItems) return result(Status::ResourceLimit, start);
                ++items; // Every member consumes input; increment cannot wrap.
                if (end == p.end) break;
                start = end + 1;
            }
        }
        i = next(p, value.size);
    }
    if (!(presence & FREQ)) return result(Status::MalformedRule, value.size);
    if (extension != value.size) return result(Status::UnsupportedFeature, extension);

    // Pass 2: core representations in input order. No source context is supplied.
    Frequency freq = Frequency::Unknown;
    bool leap = false;
    size_t ordinal = value.size;
    for (size_t i = 0; i < value.size;) {
        Part p = {}; part(value, i, p); // Already framed; input is immutable during the call.
        Report r = result(Status::CoreGrammarValid);
        const View v = {value.data + p.value, p.end - p.value};
        if (p.id == Id::Freq) {
            freq = frequency(v);
            if (freq == Frequency::Unknown) return result(Status::MalformedRule, p.value);
        } else if (p.id == Id::Until) {
            IcsTypedValues::Date date = {}; IcsTypedValues::DateTime time = {};
            const auto typed = v.size == 8 ? IcsTypedValues::decodeDate(v, date) : IcsTypedValues::decodeDateTime(v, time);
            if (!IcsTypedValues::parsed(typed)) return result(Status::MalformedRule, p.value + typed.offset);
            if (typed.status == IcsTypedValues::Status::NeedsContext) leap = true;
        } else if (p.id == Id::Count || p.id == Id::Interval) r = positive(value, p);
        else if (p.id == Id::Wkst) {
            if (!weekday(v)) return result(Status::MalformedRule, p.value);
        } else if (list(p.id)) {
            size_t start = p.value;
            for (size_t end = start;; ++end) if (end == p.end || value.data[end] == ',') {
                r = member(value, p.id, start, end, ordinal, leap);
                if (r.status != Status::CoreGrammarValid) return r;
                if (end == p.end) break;
                start = end + 1;
            }
        }
        if (r.status != Status::CoreGrammarValid) return r;
        i = next(p, value.size);
    }

    // Pass 3: locally decidable relationships in part order, now FREQ is known.
    // UNTIL/COUNT diagnoses the later conflicting name; other relationships name
    // the offending BY part (numeric BYDAY diagnoses its first ordinal member).
    uint32_t seen = 0;
    const uint32_t otherBy = BYSECOND | BYMINUTE | BYHOUR | BYDAY | BYMONTHDAY | BYYEARDAY | BYWEEKNO | BYMONTH;
    for (size_t i = 0; i < value.size;) {
        Part p = {}; part(value, i, p);
        if ((p.id == Id::Until && (seen & COUNT)) || (p.id == Id::Count && (seen & UNTIL)) ||
            (p.id == Id::ByMonthDay && freq == Frequency::Weekly) ||
            (p.id == Id::ByYearDay && (freq == Frequency::Daily || freq == Frequency::Weekly || freq == Frequency::Monthly)) ||
            (p.id == Id::ByWeekNo && freq != Frequency::Yearly) ||
            (p.id == Id::BySetPos && !(presence & otherBy))) return result(Status::MalformedRule, p.start);
        if (p.id == Id::ByDay && ordinal != value.size &&
            ((freq != Frequency::Monthly && freq != Frequency::Yearly) || (freq == Frequency::Yearly && (presence & BYWEEKNO))))
            return result(Status::MalformedRule, ordinal);
        seen |= bit(p.id); i = next(p, value.size);
    }
    return {Status::CoreGrammarValid, 0, freq, presence, leap};
}
}
#endif
